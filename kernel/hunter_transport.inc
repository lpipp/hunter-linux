/* SPDX-License-Identifier: GPL-2.0 */
/* This file replaces Utils in the existing distribution Huawei driver. */
static int huawei_wmi_raw(void *context, const u8 *input, unsigned int length,
			  struct hunter_attempt *trace)
{
	struct huawei_wmi *huawei = context;
	struct acpi_buffer in = { length, (void *)input };
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj, *elements;
	u64 start = ktime_get_ns();
	acpi_status status;
	int err = -EPROTO;

	lockdep_assert_held(&huawei->wmi_lock);
	status = wmi_evaluate_method(HWMI_METHOD_GUID, 0, 1, &in, &out);
	trace->acpi_status = status;
	if (ACPI_FAILURE(status)) {
		err = -EIO;
		goto done;
	}
	obj = out.pointer;
	if (!obj)
		goto done;
	if (obj->type == ACPI_TYPE_BUFFER) {
		err = hunter_adapt(HUNTER_FORMAT_FLAT260, NULL, 0,
			obj->buffer.pointer, obj->buffer.length, trace);
	} else if (obj->type == ACPI_TYPE_PACKAGE &&
		   obj->package.count == 2 && obj->package.elements) {
		elements = obj->package.elements;
		if (elements[0].type == ACPI_TYPE_BUFFER &&
		    elements[1].type == ACPI_TYPE_BUFFER)
			err = hunter_adapt(HUNTER_FORMAT_PACKAGE,
				elements[0].buffer.pointer, elements[0].buffer.length,
				elements[1].buffer.pointer, elements[1].buffer.length, trace);
	}
done:
	/* Fresh ACPI_ALLOCATE_BUFFER for every attempt; never adjust ownership. */
	kfree(out.pointer);
	trace->duration_ns = ktime_get_ns() - start;
	return err;
}

static int huawei_wmi_transaction(struct huawei_wmi *huawei, const u8 *input,
		unsigned int length, bool legacy, struct hunter_request *result)
{
	int err;
	mutex_lock(&huawei->wmi_lock);
	if (huawei->hunter.stopped ||
	    (!legacy && (!huawei->hunter.matched || huawei->hunter.removing)))
		err = -ENODEV;
	else if (huawei->hunter.suspended)
		err = -EBUSY;
	else
		err = hunter_transact(huawei, huawei_wmi_raw, input, length,
				      legacy, result);
	mutex_unlock(&huawei->wmi_lock);
	return err;
}

/* Legacy commands preserve 8-byte inputs and the historical retry policy,
 * but now the mutex covers both attempts and each ACPI result is freed.
 */
static int huawei_wmi_cmd(u64 arg, u8 *buf, size_t buflen)
{
	struct hunter_request result = {};
	int err;
	err = huawei_wmi_transaction(huawei_wmi, (u8 *)&arg, sizeof(arg),
				     true, &result);
	if (result.attempts && result.trace[result.attempts - 1].format && buf)
		memcpy(buf, result.trace[result.attempts - 1].payload,
		       min_t(size_t, buflen, 256));
	return err == -EREMOTEIO ? -ENODEV : err;
}
