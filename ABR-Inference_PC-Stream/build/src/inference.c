#define _POSIX_C_SOURCE 200809L
#include <Python.h>
#include <numpy/arrayobject.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "inference.h"

static PyObject *g_module = NULL;
static PyObject *g_model = NULL;

static double now_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// parse binary PLY data from GByteArray into a float array (x,y,z) and return count
// Caller must free *out_points
static float* parse_ply_binary_to_floats(GByteArray* arr, int* out_n) {
    if (!arr) return NULL;
    unsigned char* data = arr->data;
    size_t size = arr->len;

    char* header_end = NULL;
    for (size_t i = 0; i + 10 < size; i++) {
        if (memcmp(data + i, "end_header", 10) == 0) {
            header_end = (char*)(data + i + 10);
            break;
        }
    }
    if (!header_end) return NULL;
    while (*header_end == '\n' || *header_end == '\r') header_end++;

    size_t header_len = header_end - (char*)data;
    char* header_str = (char*)malloc(header_len + 1);
    memcpy(header_str, data, header_len);
    header_str[header_len] = '\0';

    int vertex_count = 0;
    char* saveptr = NULL;
    char* line = strtok_r(header_str, "\n", &saveptr);
    while (line) {
        if (strncmp(line, "element vertex", 14) == 0) {
            sscanf(line, "element vertex %d", &vertex_count);
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(header_str);
    if (vertex_count <= 0) return NULL;

    double* src = (double*)header_end;
    float* points = malloc(vertex_count * 3 * sizeof(float));
    if (!points) return NULL;
    for (int i = 0; i < vertex_count; i++) {
        points[i*3 + 0] = (float)src[i*3 + 0];
        points[i*3 + 1] = (float)src[i*3 + 1];
        points[i*3 + 2] = (float)src[i*3 + 2];
    }
    *out_n = vertex_count;
    return points;
}


void* run_numpy_import() {
    import_array();
    return NULL;
}



int inference_init(const char* model_path) {
    if (!model_path) model_path = "rf_cross_50t_d12_trees.npz";
    if (Py_IsInitialized()) return 0;

    Py_Initialize();
    run_numpy_import(); // Call this instead of calling import_array() directly

    // print Python sys.path for debug (optional)
    PyRun_SimpleString("import sys; sys.path.append('.')");

    PyObject *pName = PyUnicode_FromString("rf_sr_api");
    if (!pName) return -1;
    g_module = PyImport_Import(pName);
    Py_DECREF(pName);
    if (!g_module) {
        PyErr_Print();
        return -2;
    }

    // call init_predictor(model_path)
    PyObject *init_func = PyObject_GetAttrString(g_module, "init_predictor");
    if (init_func && PyCallable_Check(init_func)) {
        PyObject *arg = PyTuple_Pack(1, PyUnicode_FromString(model_path));
        g_model = PyObject_CallObject(init_func, arg);
        Py_DECREF(arg);
        Py_DECREF(init_func);
        if (!g_model) {
            PyErr_Print();
            Py_DECREF(g_module);
            g_module = NULL;
            return -3;
        }
    } else {
        if (init_func) Py_DECREF(init_func);
        PyErr_Print();
        Py_DECREF(g_module);
        g_module = NULL;
        return -4;
    }

    return 0;
}

int inference_run_buffer(GByteArray* ply_buf, double* inference_ms) {
    if (!ply_buf) return -1;
    if (!g_module) return -2;
    double t0 = now_sec();

    int N = 0;
    float* points = parse_ply_binary_to_floats(ply_buf, &N);
    if (!points) return -3;

    // Debug: print first up to 10 parsed points
    // fprintf(stderr, "[inference] parsed %d points\n", N);
    // int show = N < 10 ? N : 10;
    // for (int i = 0; i < show; i++) {
    //     fprintf(stderr, "[inference] point %d: %f %f %f\n", i,
    //             points[i*3 + 0], points[i*3 + 1], points[i*3 + 2]);
    // }

    // Create numpy array (N x 3) using points (will copy because we cannot rely on caller's memory lifetime)
    npy_intp dims[2] = {N, 3};
    PyObject* np_array = PyArray_SimpleNewFromData(2, dims, NPY_FLOAT32, points);
    if (!np_array) {
        free(points);
        return -4;
    }

    // Ensure array owns its data so Python can manage it (make copy)
    PyObject* np_copy = PyArray_NewCopy((PyArrayObject*)np_array, NPY_ANYORDER);
    Py_DECREF(np_array);
    free(points);
    if (!np_copy) return -5;

    PyObject *infer_func = PyObject_GetAttrString(g_module, "run_inference");
    if (!infer_func || !PyCallable_Check(infer_func)) {
        if (infer_func) Py_DECREF(infer_func);
        Py_DECREF(np_copy);
        return -6;
    }

    PyObject *args = PyTuple_Pack(1, np_copy);
    Py_DECREF(np_copy);
    PyObject *result = PyObject_CallObject(infer_func, args);
    Py_DECREF(args);
    Py_DECREF(infer_func);

    if (!result) {
        PyErr_Print();
        return -7;
    }

    // Expect result to be a numpy array of floats; for logging we collapse to a single label
    // discard numeric output here; only measure time
    Py_DECREF(result);

    double t1 = now_sec();
    if (inference_ms) *inference_ms = (t1 - t0) * 1000.0;
    return 0;
}

void inference_shutdown(void) {
    if (g_model) { Py_DECREF(g_model); g_model = NULL; }
    if (g_module) { Py_DECREF(g_module); g_module = NULL; }
    if (Py_IsInitialized()) Py_Finalize();
}
