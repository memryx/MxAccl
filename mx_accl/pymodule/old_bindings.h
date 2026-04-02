// Old bindings for compatibility with deprecated Python runtime API

#include <pybind11/pybind11.h>
// has all the gbf convert stuff
#include "convert.h"

static PyObject *stream_ifmap(PyObject *self, PyObject *args,
                              PyObject *kwargs) {
    bool status;
    uint8_t flow_id;
    PyArrayObject *ifmap;
    PyObject *Py_client;

    uint16_t height = 0;
    uint16_t width = 0;
    uint16_t z = 0;
    uint32_t num_ch = 0;
    uint32_t tensor_size = 0;
    uint64_t fmt_size = 0;
    uint8_t format = 0;
    PyArrayObject *buffer;

    // parse parameters
    static char *kwlist[] = {"flow_id",     "ifmap",  "client", "fmt_size",
                             "tensor_size", "format", "dim_h",  "dim_w",
                             "dim_z",       "dim_c",  "buffer", nullptr};
    if (!PyArg_ParseTupleAndKeywords(
            args, kwargs, "bO!OKIbHHHIO!", kwlist, &flow_id, &PyArray_Type,
            &ifmap, &Py_client, &fmt_size, &tensor_size, &format, &height,
            &width, &z, &num_ch, &PyArray_Type, &buffer)) {
        PyErr_BadArgument();
        return nullptr;
    }

    // convert to client
    py::handle handle_client(Py_client);
    MX::Runtime::Client *client = handle_client.cast<MX::Runtime::Client *>();

    uint8_t *formatted_data = (uint8_t *)PyArray_DATA(buffer);

    {
        // printf("input shape info => height: %d, width: %d, z: %d, num_ch: %d,
        // format: %d, tensor_size: %d\n", height, width, z, num_ch, format,
        // tensor_size);

        if (format == MEMX_FMAP_FORMAT_BF16) {
            // BF convert
            Py_INCREF(ifmap);
            Py_INCREF(buffer);
            Py_BEGIN_ALLOW_THREADS

                convert_bf16((uint32_t *)PyArray_DATA(ifmap), formatted_data,
                             tensor_size);

            // send
            status = client->send(formatted_data, fmt_size);
            // printf("[client %d] send ifmap data size: %d\n",
            // client->my_client_id, fmt_size);

            Py_END_ALLOW_THREADS Py_DECREF(buffer);
            Py_DECREF(ifmap);
        } else if (format == MEMX_FMAP_FORMAT_GBF80) {
            Py_INCREF(ifmap);
            Py_INCREF(buffer);
            Py_BEGIN_ALLOW_THREADS

                convert_gbf((uint32_t *)PyArray_DATA(ifmap), formatted_data,
                            tensor_size, num_ch);

            // send
            status = client->send(formatted_data, fmt_size);
            // printf("[client %d] send ifmap data size: %d\n",
            // client->my_client_id, fmt_size);

            Py_END_ALLOW_THREADS Py_DECREF(buffer);
            Py_DECREF(ifmap);
        } else if (format == MEMX_FMAP_FORMAT_GBF80_ROW_PAD) {
            // GBF row pad
            Py_INCREF(ifmap);
            Py_INCREF(buffer);
            Py_BEGIN_ALLOW_THREADS

                convert_gbf_row_pad((uint32_t *)PyArray_DATA(ifmap),
                                    formatted_data, height, width, z, num_ch);

            // send
            status = client->send(formatted_data, fmt_size);
            // printf("[client %d] send ifmap data size: %d\n",
            // client->my_client_id, fmt_size);

            Py_END_ALLOW_THREADS Py_DECREF(buffer);
            Py_DECREF(ifmap);
        } else {
            // don't convert anything else
            Py_INCREF(ifmap);
            Py_BEGIN_ALLOW_THREADS status =
                client->send((uint8_t *)PyArray_DATA(ifmap), fmt_size);
            // printf("[client %d] send ifmap data size: %d\n",
            // client->my_client_id, fmt_size);
            Py_END_ALLOW_THREADS Py_DECREF(ifmap);
        }
    }

    unused(self);
    return Py_BuildValue("i", status);
}

static PyObject *stream_ofmap(PyObject *self, PyObject *args,
                              PyObject *kwargs) {
    bool status;
    uint8_t flow_id;
    PyArrayObject *ofmap;
    PyObject *Py_client;

    uint16_t height = 0;
    uint16_t width = 0;
    uint16_t z = 0;
    uint32_t num_ch = 0;
    uint32_t tensor_size = 0;
    uint64_t fmt_size = 0;
    uint8_t format = 0;
    uint8_t hpoc_enabled = 0;
    int hpoc_size = 0;
    PyArrayObject *hpoc_indexes = nullptr;
    PyArrayObject *buffer = nullptr;

    // parse parameters
    static char *kwlist[] = {"flow_id",      "ofmap",        "client",
                             "fmt_size",     "tensor_size",  "format",
                             "dim_h",        "dim_w",        "dim_z",
                             "dim_c",        "hpoc_enabled", "hpoc_extra_size",
                             "hpoc_indexes", "buffer",       nullptr};
    if (!PyArg_ParseTupleAndKeywords(
            args, kwargs, "bO!OKIbHHHIbIO!O!", kwlist, &flow_id, &PyArray_Type,
            &ofmap, &Py_client, &fmt_size, &tensor_size, &format, &height,
            &width, &z, &num_ch, &hpoc_enabled, &hpoc_size, &PyArray_Type,
            &hpoc_indexes, &PyArray_Type, &buffer)) {
        PyErr_BadArgument();
        return nullptr;
    }

    // convert to client
    py::handle handle_client(Py_client);
    MX::Runtime::Client *client = handle_client.cast<MX::Runtime::Client *>();

    uint8_t *formatted_data = (uint8_t *)PyArray_DATA(buffer);

    {
        // printf("output shape info => height: %d, width: %d, z: %d, num_ch:
        // %d, format: %d, tensor_size: %d\n", height, width, z, num_ch, format,
        // tensor_size);

        if (format == MEMX_FMAP_FORMAT_BF16) {
            Py_INCREF(ofmap);
            Py_INCREF(buffer);
            Py_BEGIN_ALLOW_THREADS

                // printf("[client %d] About to receive ofmap data size: %d\n",
                // client->my_client_id, fmt_size);
                status = client->recv(formatted_data, fmt_size);
            // printf("[client %d] Finish receiving ofmap\n",
            // client->my_client_id);

            // BF unconvert
            unconvert_bf16(formatted_data, (uint32_t *)PyArray_DATA(ofmap),
                           tensor_size);

            Py_END_ALLOW_THREADS Py_DECREF(buffer);
            Py_DECREF(ofmap);

        } else if (format == MEMX_FMAP_FORMAT_GBF80) {
            Py_INCREF(ofmap);
            Py_INCREF(buffer);
            Py_INCREF(hpoc_indexes);
            Py_BEGIN_ALLOW_THREADS

                // printf("[client %d] About to receive ofmap data size: %d\n",
                // client->my_client_id, fmt_size);
                status = client->recv(formatted_data, fmt_size);
            // printf("[client %d] Finish receiving ofmap\n",
            // client->my_client_id);

            // GBF unconvert
            if (hpoc_enabled != 0) {
                unconvert_gbf_hpoc(formatted_data,
                                   (uint32_t *)PyArray_DATA(ofmap), height,
                                   width, z, num_ch, hpoc_size,
                                   (int *)PyArray_DATA(hpoc_indexes), 0);
            } else {
                unconvert_gbf(formatted_data, (uint32_t *)PyArray_DATA(ofmap),
                              tensor_size, num_ch);
            }

            Py_END_ALLOW_THREADS Py_DECREF(ofmap);
            Py_DECREF(buffer);
            Py_DECREF(hpoc_indexes);

        } else if (format == MEMX_FMAP_FORMAT_GBF80_ROW_PAD) {
            Py_INCREF(ofmap);
            Py_INCREF(buffer);
            Py_INCREF(hpoc_indexes);
            Py_BEGIN_ALLOW_THREADS

                // printf("[client %d] About to receive ofmap data size: %d\n",
                // client->my_client_id, fmt_size);
                status = client->recv(formatted_data, fmt_size);
            // printf("[client %d] Finish receiving ofmap\n",
            // client->my_client_id);

            // GBF unconvert
            if (hpoc_enabled != 0) {
                unconvert_gbf_hpoc(formatted_data,
                                   (uint32_t *)PyArray_DATA(ofmap), height,
                                   width, z, num_ch, hpoc_size,
                                   (int *)PyArray_DATA(hpoc_indexes), 1);
            } else {
                unconvert_gbf_row_pad(formatted_data,
                                      (uint32_t *)PyArray_DATA(ofmap), height,
                                      width, z, num_ch);
            }

            Py_END_ALLOW_THREADS Py_DECREF(ofmap);
            Py_DECREF(buffer);
            Py_DECREF(hpoc_indexes);

        } else {
            // don't convert anything else
            Py_INCREF(ofmap);
            Py_BEGIN_ALLOW_THREADS
                // printf("[client %d] About to receive ofmap data size: %d\n",
                // client->my_client_id, fmt_size);
                status = client->recv((uint8_t *)PyArray_DATA(ofmap), fmt_size);
            // printf("[client %d] Finish receiving ofmap\n",
            // client->my_client_id);
            Py_END_ALLOW_THREADS Py_DECREF(ofmap);
        }
    }

    unused(self);
    return Py_BuildValue("i", status);
}

// Forward declaration for registering raw C API functions
static PyMethodDef py_methods[] = {
    {"stream_ifmap", (PyCFunction)stream_ifmap, METH_VARARGS | METH_KEYWORDS,
     "stream_ifmap in shared mode"},
    {"stream_ofmap", (PyCFunction)stream_ofmap, METH_VARARGS | METH_KEYWORDS,
     "stream_ofmap in shared mode"},
    {nullptr, nullptr, 0, nullptr}};


