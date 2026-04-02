// Must come BEFORE any pybind11 includes
#define PYBIND11_NO_ASSERT_GIL_HELD_INCREF_DECREF

#include <pybind11/functional.h> // for std::function
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>        // for py::bytes
#include <pybind11/stl.h>            // for std::string
#include <pybind11/stl/filesystem.h> // for std::filesystem::path

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>
#include <numpy/ndarrayobject.h>
#include <numpy/ndarraytypes.h>

#include <memx/accl/MxAccl.h>
#include <memx/accl/MxAcclMT.h>
#include <memx/accl/MxModel.h>
#include <memx/accl/dfp.h>
#include <memx/accl/messages.h>
#include <memx/accl/utils/featureMap.h>
#include <memx/accl/utils/mxTypes.h>
#include <memx/accl/utils/macros.h>

#include <string>

namespace py = pybind11;

using namespace MX::Runtime;
using namespace MX::RPC;

class BindObj; // forward declaration
class BindObjMT; // forward declaration

/**
 * @brief PyMxAccl extends the MxAccl class to provide additional functionality 
 * for Python bindings. It maintains binding objects for each model 
 * and handles exceptions raised from asynchronous callbacks.
 */
class PyMxAccl : public MX::Runtime::MxAccl {
public:
    // Constructor: from dfp path
    PyMxAccl(const std::filesystem::path &dfp_path,
    std::vector<int> device_ids_to_use = {0},
             std::array<bool, 2> use_model_shape = {true, true},
             bool local_mode = false,
             SchedulerOptions sched_options = {20, 250, 16, 21, false, 11500, false, 50, 6},
             ClientOptions client_options = {false, 0},
             std::string server_addr = "/run/mxa_manager/",
             unsigned int server_port_base = 10000,
             bool ignore_server = false);

    // Constructor: from dfp bytes
    PyMxAccl(uint8_t *dfp_bytes, size_t dfp_byte_size,
             std::vector<int> device_ids_to_use = {0},
             std::array<bool, 2> use_model_shape = {true, true},
             bool local_mode = false,
             SchedulerOptions sched_options = {20, 250, 16, 21, false, 11500, false, 50, 6},
             ClientOptions client_options = {false, 0},
             std::string server_addr = "/run/mxa_manager/",
             unsigned int server_port_base = 10000,
             bool ignore_server = false);

    ~PyMxAccl();

    void rethrow_exception_if_any();
    BindObj *get_binding_obj(int model_id);

    std::vector<BindObj *> bobjs; // binding objects for each model

    //  MxAccl uses std::thread to achieve asynchronous callbacks functionality.
    //  We need to store exception that occurs inside child thread, and later
    //  rethrow in main thread.
    std::exception_ptr eptr;       // pointer to hold exception from callback
};

/**
 * @brief PyMxAcclMT extends the MxAcclMT class to provide additional
 * functionality for Python bindings. It maintains binding objects
 * for each model.
 */
class PyMxAcclMT : public MX::Runtime::MxAcclMT {
public:
    PyMxAcclMT(const std::filesystem::path &dfp_path,
               std::vector<int> device_ids_to_use = {0},
               std::array<bool, 2> use_model_shape = {true, true},
               bool local_mode = false,
               SchedulerOptions sched_options = {20, 250, 16, 21, false, 11500, false, 50, 6},
               ClientOptions client_options = {false, 0},
               std::string server_addr = "/run/mxa_manager/",
               unsigned int server_port_base = 10000,
               bool ignore_server = false);
    
    // Constructor: from dfp bytes
    PyMxAcclMT(uint8_t *dfp_bytes, size_t dfp_byte_size,
               std::vector<int> device_ids_to_use = {0},
               std::array<bool, 2> use_model_shape = {true, true},
               bool local_mode = false,
               SchedulerOptions sched_options = {20, 250, 16, 21, false, 11500, false, 50, 6},
               ClientOptions client_options = {false, 0},
               std::string server_addr = "/run/mxa_manager/",
               unsigned int server_port_base = 10000,
               bool ignore_server = false);

    ~PyMxAcclMT();

    BindObjMT *get_binding_obj(int model_id);

    std::vector<BindObjMT *> bobjs; // binding objects for each model
};


/**
 * @brief Base class for binding objects that handles common functionality
 *
 * Provides shared functionality for model info management, shape handling,
 * and buffer allocation between synchronous and asynchronous binding objects.
 */
class BindObjBase {
protected:
    MX::Types::MxModelInfo model_info;
    MX::Types::MxModelInfo post_model_info;
    MX::Types::MxModelInfo pre_model_info;
    std::unordered_map<int, std::vector<float *>> ofmap_buffers_map; // stream_id -> list of input buffers
    std::vector<std::vector<int64_t>> ofmap_shapes;
    std::vector<std::vector<int64_t>> ifmap_shapes;
    int model_id;

    explicit BindObjBase(int model_id) : model_id(model_id) {}

    virtual ~BindObjBase();

    void setup(const MX::Types::MxModelInfo &info);

    void prepare_input(const py::object &in_data, std::vector<float*> &fmap_ptrs);

    void clear_output_buffers();

    void clear_input_buffers();

    void update_pre_model_info(const MX::Types::MxModelInfo &pre_info);

    void update_post_model_info(const MX::Types::MxModelInfo &post_info);

    void convert_obj_to_numpy(const py::object &obj, py::array &arr);

    template <typename T> std::string get_shape_str(const std::vector<T> &vec) {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < vec.size(); i++) {
            oss << vec[i];
            if (i != vec.size() - 1)
                oss << ", ";
        }
        oss << "]";
        return oss.str();
    }

    // Convert a raw float pointer (C-style buffer) into a NumPy array view
    // without copying data. The pointer `ptr` must remain valid while Python
    // is using the NumPy array.
    void construct_numpy_from_ptr(py::array_t<float> *np_array,
                                  const float *ptr,
                                  const std::vector<int64_t> &raw_shape);

    void ensure_valid_input(const py::array &arr, const py::buffer_info &buffer,
                            const std::vector<int64_t> &expected_shape);

    void preallocate_buffers(const std::vector<std::vector<int64_t>> &shapes,
                             std::vector<float *> &buffers);

    // one mutex per stream to protect ofmap_buffers_map
    ThreadSafeMap<int, std::unique_ptr<std::mutex>> obuffer_mtxs;

  private:
    void setup_shapes();
};

/**
 * @brief Asynchronous binding object for PyMxAccl
 * 
 * Handles asynchronous operations with callback-based data flow.
 * Manages GIL acquisition/release for thread safety with Python callbacks.
 */
class BindObj : public BindObjBase {
private:
    std::unordered_map<int, py::object> py_in_cb_map;
    std::unordered_map<int, py::object> py_out_cb_map;
    PyMxAccl *pyaccl;
    std::atomic<bool> running;

public:
    BindObj(PyMxAccl *pyaccl, int model_id);

    void connect_stream(int stream_id, py::object &in_cb, py::object &out_cb);

    void connect_pre_model(const std::filesystem::path &pre_model_path, int model_id);

    void connect_post_model(const std::filesystem::path &post_model_path,
                            int model_id,
                            const std::vector<long unsigned int> &post_size_list);
    
    bool  set_operating_frequency(int device_id, int freq);
    float get_power(int device_id);
    bool  can_get_power_consumption(int device_id);
    float get_max_temperature(int device_id);
    py::array_t<float> get_chip_temperatures(int device_id);

private:
    void call_py_in_cb(py::object *obj, int stream_id);

    bool in_callback(std::vector<const MX::Types::FeatureMap *> ifmaps, int stream_id);

    bool out_callback(std::vector<const MX::Types::FeatureMap *> ofmaps, int stream_id);
};

/**
 * @brief Synchronous (Manual Threading) binding object for PyMxAcclMT
 * 
 * Handles synchronous operations where the caller manages threading.
 * Used for blocking send/receive operations.
 */
class BindObjMT : public BindObjBase {
private:
    PyMxAcclMT *pyaccl;

public:
    BindObjMT(PyMxAcclMT *pyaccl, int model_id);

    void connect_pre_model(const std::filesystem::path &pre_model_path, int model_id);
    
    void connect_post_model(const std::filesystem::path &post_model_path,
                            int model_id,
                            const std::vector<long unsigned int> &post_size_list);
    
    bool  set_operating_frequency(int device_id, int freq);
    float get_power(int device_id);
    bool  can_get_power_consumption(int device_id);
    float get_max_temperature(int device_id);
    py::array_t<float> get_chip_temperatures(int device_id);

    void send_input(const py::object &in_data, int model_id,
                    int stream_id, int timeout);

    void receive_output(std::vector<py::array_t<float>> &np_arrs, int model_id,
                        int stream_id, int timeout);

    void run(const py::array_t<float> &in_data,
             std::vector<py::array_t<float>> &out_data,
             int model_id, int stream_id,
             int32_t timeout);
};
