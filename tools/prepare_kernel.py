#!/usr/bin/env python3
"""Extend a pinned distribution driver; refuse unfamiliar source shapes.

No firmware calls, module installation or system source modification.
"""
import argparse
import hashlib
import json
from pathlib import Path
import difflib

ROOT = Path(__file__).resolve().parents[1]


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise SystemExit("Source shape changed; review required: " + old[:70])
    return text.replace(old, new, 1)


def generate(source):
    text = source
    text = replace_once(text, "#include <acpi/battery.h>", '#include <acpi/battery.h>\n#include "hunter_state.h"')
    text = replace_once(text, "struct mutex wmi_lock;", "struct mutex wmi_lock;\n\tstruct hunter_state hunter;")
    start, end = text.index("/* Utils */"), text.index("/* LEDs */")
    text = text[:start] + '#include "hunter_transport.inc"\n\n' + text[end:]
    start = text.index("static int huawei_wmi_debugfs_call_show(")
    end = text.index("DEFINE_SHOW_ATTRIBUTE(huawei_wmi_debugfs_call);", start)
    text = text[:start] + '''static int huawei_wmi_debugfs_call_show(struct seq_file *m, void *data)
{
\tstruct huawei_wmi *huawei = m->private;
\tstruct hunter_request result = {};
\tu64 arg = READ_ONCE(huawei->debug.arg);
\tunsigned int i;
\tint err;
\terr = huawei_wmi_transaction(huawei, (u8 *)&arg, sizeof(arg), true, &result);
\tfor (i = 0; i < result.attempts; i++) {
\t\tseq_printf(m, "attempt=%u status=%u error=%d duration_ns=%llu\\n",
\t\t\ti + 1, result.trace[i].status, result.trace[i].error,
\t\t\tresult.trace[i].duration_ns);
\t\tif (result.trace[i].format)
\t\t\tseq_hex_dump(m, "", DUMP_PREFIX_NONE, 16, 1,
\t\t\t\t     result.trace[i].payload, 256, false);
\t}
\treturn err;
}

''' + text[end:]
    # Remove the obsolete raw ACPI debug dumper. Debugfs uses the common transaction.
    start = text.index("static void huawei_wmi_debugfs_call_dump(")
    end = text.index("static int huawei_wmi_debugfs_call_show(", start)
    text = text[:start] + text[end:]
    text = replace_once(text, "/* Huawei driver */", '#include "hunter_platform.inc"\n\n/* Huawei driver */')
    text = replace_once(text, "huawei_wmi->dev = &pdev->dev;", """huawei_wmi->dev = &pdev->dev;
\tmutex_init(&huawei_wmi->wmi_lock);
\thuawei_wmi->hunter.stopped = false;
\thuawei_wmi->hunter.removing = false;
\thuawei_wmi->hunter.suspended = false;""")
    text = replace_once(text, "\t\tmutex_init(&huawei_wmi->wmi_lock);\n", "")
    text = replace_once(text, "\t\thuawei_wmi_debugfs_setup(&pdev->dev);", """\t\thuawei_wmi_debugfs_setup(&pdev->dev);
\t\terr = hunter_setup(huawei_wmi);
\t\tif (err)
\t\t\tdev_warn(&pdev->dev, "Hunter interfaces unavailable: %d\\n", err);""")
    text = replace_once(text, "static void huawei_wmi_remove(struct platform_device *pdev)\n{", "static void huawei_wmi_remove(struct platform_device *pdev)\n{")
    start = text.index("static void huawei_wmi_remove(")
    pos = text.index("\n\twhile (*guid->guid_string)", start)
    text = text[:pos] + "\n\thunter_stop(huawei_wmi);\n" + text[pos:]
    text = replace_once(text, "\tdevm_led_classdev_register(dev, &huawei->cdev);", """\thuawei->hunter.legacy_led_registered =
\t\t!devm_led_classdev_register(dev, &huawei->cdev);""")
    text = replace_once(text, "\t\thuawei_wmi_fn_lock_exit(&pdev->dev);", """\t\thuawei_wmi_fn_lock_exit(&pdev->dev);
\t\t/* LED unregister flushes work and issues its legacy LED-off command.
\t\t * Keep the shared transport alive until that cleanup finishes.
\t\t */
\t\tif (huawei_wmi->hunter.legacy_led_registered) {
\t\t\tdevm_led_classdev_unregister(&pdev->dev, &huawei_wmi->cdev);
\t\t\thuawei_wmi->hunter.legacy_led_registered = false;
\t\t}
\t\tmutex_lock(&huawei_wmi->wmi_lock);
\t\thuawei_wmi->hunter.stopped = true;
\t\tmutex_unlock(&huawei_wmi->wmi_lock);""")
    text = replace_once(text, '\t\t.name = "huawei-wmi",', '\t\t.name = "huawei-wmi",\n\t\t.pm = pm_sleep_ptr(&hunter_pm_ops),')
    text = replace_once(text, "\thuawei_wmi = kzalloc_obj(struct huawei_wmi);",
                        "\thuawei_wmi = kzalloc(sizeof(*huawei_wmi), GFP_KERNEL);")
    text += '\nMODULE_VERSION("hunter-0.3.0");\n'
    return text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=ROOT / "kernel/vendor/huawei-wmi.c")
    args = parser.parse_args()
    original = args.source.read_text()
    if args.source.resolve() == (ROOT / "kernel/vendor/huawei-wmi.c").resolve():
        digest = hashlib.sha256(args.source.read_bytes()).hexdigest()
        if digest != "292567169949abc7f1bd861b1e37efdfe0d5cef71658252466ba898db2c6d482":
            parser.error("Pinned Ubuntu source checksum mismatch")
    generated = generate(original)
    # The only OEM evaluate site must live in the common raw helper.
    if "wmi_evaluate_method(" in generated or "huawei_wmi_call(" in generated:
        parser.error("Unexpected OEM call bypass")
    (ROOT / "kernel/huawei-wmi.c").write_text(generated)
    diff = difflib.unified_diff(original.splitlines(True), generated.splitlines(True),
                                fromfile="a/drivers/platform/x86/huawei-wmi.c",
                                tofile="b/drivers/platform/x86/huawei-wmi.c")
    patch = "".join(diff)
    for name in ("hunter_state.h", "hunter_protocol.h", "hunter_wire.h", "hunter_transport.inc", "hunter_platform.inc"):
        patch += "".join(difflib.unified_diff([], (ROOT / "kernel" / name).read_text().splitlines(True),
                                             fromfile="/dev/null", tofile="b/drivers/platform/x86/" + name))
    (ROOT / "kernel/huawei-wmi-hunter.patch").write_text(patch)
    record = {"source": str(args.source.resolve()),
              "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
              "generated_sha256": hashlib.sha256(generated.encode()).hexdigest()}
    (ROOT / "kernel/source-manifest.json").write_text(json.dumps(record, indent=2) + "\n")
    print("Prepared Huawei driver with one common OEM transaction entry")


if __name__ == "__main__":
    main()
