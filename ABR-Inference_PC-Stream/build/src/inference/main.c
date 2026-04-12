#include <Python.h>
#include <numpy/arrayobject.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MODEL_PATH "rf_sr_cross_liv2off.joblib"

double now() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// ------------------------------------------------------------
// Load file into GByteArray
// ------------------------------------------------------------
GByteArray* load_file_to_gbytearray(const char* path) {
    GByteArray *array = g_byte_array_new();
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("fopen");
        return NULL;
    }

    char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        g_byte_array_append(array, (guint8*)buffer, n);
    }

    fclose(f);
    return array;
}


float* parse_ply_binary(GByteArray* arr, int* out_n) {
    unsigned char* data = arr->data;
    size_t size = arr->len;

    // --- find end_header ---
    char* header_end = NULL;
    for (size_t i = 0; i + 10 < size; i++) {
        if (memcmp(data + i, "end_header", 10) == 0) {
            header_end = (char*)(data + i + 10);
            break;
        }
    }

    if (!header_end) {
        printf("❌ end_header not found\n");
        return NULL;
    }

    while (*header_end == '\n' || *header_end == '\r') header_end++;

    // --- extract header safely ---
    size_t header_len = header_end - (char*)data;
    char* header_str = (char*)malloc(header_len + 1);
    memcpy(header_str, data, header_len);
    header_str[header_len] = '\0';

    // --- parse vertex count ---
    int vertex_count = 0;
    char* line = strtok(header_str, "\n");
    while (line) {
        if (strncmp(line, "element vertex", 14) == 0) {
            sscanf(line, "element vertex %d", &vertex_count);
        }
        line = strtok(NULL, "\n");
    }
    free(header_str);

    if (vertex_count <= 0) {
        printf("❌ Invalid vertex count\n");
        return NULL;
    }

    // --- read binary data ---
    double* src = (double*)header_end;

    float* points = malloc(vertex_count * 3 * sizeof(float));

    for (int i = 0; i < vertex_count; i++) {
        points[i*3 + 0] = (float)src[i*3 + 0];
        points[i*3 + 1] = (float)src[i*3 + 1];
        points[i*3 + 2] = (float)src[i*3 + 2];
    }

    *out_n = vertex_count;

    printf("First 10 points:\n");
        for (int i = 0; i < 10 ; i++) {
            printf("%d: %f %f %f\n",
                i,
                points[i*3 + 0],
                points[i*3 + 1],
                points[i*3 + 2]);
        }


    return points;
}



void* run_numpy_import() {
    import_array();
    return NULL;
}



// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s input.ply\n", argv[0]);
        return 1;
    }

    double t0 = now();

    // 1. Load file → GByteArray
    GByteArray* arr = load_file_to_gbytearray(argv[1]);
    double t1 = now();

    // 2. Parse PLY → float array
    int N = 0;
    float* points = parse_ply_binary(arr, &N);
    double t2 = now();

    printf("Loaded %d points\n", N);

    // 3. Init Python
    Py_Initialize();
    run_numpy_import(); // Call this instead of calling import_array() directly

    PyRun_SimpleString("import sys; import os; print(f'Searching in: {sys.path}'); print(f'Current Dir: {os.getcwd()}')");
    PyRun_SimpleString("sys.path.append('.')");

    PyObject *pName = PyUnicode_FromString("rf_sr_api");
    PyObject *pModule = PyImport_Import(pName);

    if (!pModule) {
        PyErr_Print();
        return 1;
    }

    // 4. init_model()
    PyObject *init_func = PyObject_GetAttrString(pModule, "init_model");
    PyObject *args_init = PyTuple_Pack(1, PyUnicode_FromString(MODEL_PATH));
    PyObject *model = PyObject_CallObject(init_func, args_init);

    double t3 = now();

    // 5. Convert to NumPy (zero-copy)
    npy_intp dims[2] = {N, 3};
    PyObject* np_array = PyArray_SimpleNewFromData(2, dims, NPY_FLOAT32, points);

    // 6. run_inference()
    PyObject *infer_func = PyObject_GetAttrString(pModule, "run_inference");
    PyObject *args = PyTuple_Pack(1, np_array);

    PyObject *result = PyObject_CallObject(infer_func, args);

    double t4 = now();

    // 7. Access result
    float* out = (float*) PyArray_DATA((PyArrayObject*)result);
    int out_n = PyArray_DIM((PyArrayObject*)result, 0);

    printf("Output points: %d\n", out_n);

    double t5 = now();

    // ------------------------------------------------------------
    // TIMINGS
    // ------------------------------------------------------------
    printf("\n=== Timing ===\n");
    printf("File load:      %.4f sec\n", t1 - t0);
    printf("PLY parse:      %.4f sec\n", t2 - t1);
    printf("Model load:     %.4f sec\n", t3 - t2);
    printf("Inference:      %.4f sec\n", t4 - t3);
    printf("Post-process:   %.4f sec\n", t5 - t4);

    // Cleanup
    Py_Finalize();
    g_byte_array_free(arr, TRUE);
    free(points);

    return 0;
}