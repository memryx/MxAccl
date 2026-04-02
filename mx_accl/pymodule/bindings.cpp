#include "bindings.h"
#include "old_bindings.h" // for compatibility with deprecated Python runtime API
#include "spdlog/spdlog.h"

// helper to safely call import_array()
static int numpy_import_array_wrapper() {
    import_array();  // init numpy array is required in the very beginning
    return 0;
}

// =============================================================================
// BindObjBase CLASS IMPLEMENTATION
// =============================================================================

static atomic_bool sigint_detected(false);

BindObjBase::~BindObjBase() {
    clear_input_buffers();
    clear_output_buffers();
}

void BindObjBase::setup(const MX::Types::MxModelInfo &info) {
    model_info = info;
    setup_shapes();
}

void BindObjBase::clear_output_buffers() {
    
    for (auto &pair : ofmap_buffers_map) {
        for (auto &buffer : pair.second)
            delete[] buffer;
    }
    ofmap_buffers_map.clear();
    ofmap_shapes.clear();
}

void BindObjBase::clear_input_buffers() { ifmap_shapes.clear(); }

void BindObjBase::update_pre_model_info(
    const MX::Types::MxModelInfo &pre_info) {

    // clear input only, keep output buffers
    clear_input_buffers();

    pre_model_info = pre_info;

    // TODO: refactor: init ofmap shapes when model.start() at once
    int num_ifmaps = pre_model_info.num_in_featuremaps;
    ifmap_shapes.resize(num_ifmaps);

    for (int i = 0; i < num_ifmaps; i++) {
        ifmap_shapes[i] = pre_model_info.in_featuremap_shapes[i].chlast_shape();
    }
}

void BindObjBase::prepare_input(const py::object& in_data, std::vector<float*>& fmap_ptrs) {
    // Ensure input is a list of numpy arrays
    py::list inputs;
    if (py::isinstance<py::list>(in_data)) {
        inputs = in_data;
    } else {
        // in_data --> [in_data]
        inputs.append(in_data);
    }

    int num_ifmaps = ifmap_shapes.size();
    if (inputs.size() != num_ifmaps) {
        throw std::runtime_error("Number of input feature maps does not match model requirements. Expected " + std::to_string(num_ifmaps) +
                                 ", got " + std::to_string(inputs.size()) + ".");
    }

    std::vector<py::buffer_info> buffers;

    // Validate each input and convert to numpy array
    for (size_t i = 0; i < num_ifmaps; ++i) {
        py::array arr;
        convert_obj_to_numpy(inputs[i], arr);

        buffers.push_back(arr.request());
        ensure_valid_input(arr, buffers.back(), ifmap_shapes[i]);
        fmap_ptrs.push_back((float*)(buffers.back().ptr));
    }
}

void BindObjBase::update_post_model_info(
    const MX::Types::MxModelInfo &post_info) {

    post_model_info = post_info;

    // TODO: refactor: init ofmap shapes when model.start() at once
    // update ofmap shapes
    int num_ofmaps = post_model_info.num_out_featuremaps;
    ofmap_shapes.resize(num_ofmaps);
    for (int i = 0; i < num_ofmaps; i++) {
        ofmap_shapes[i] = post_model_info.out_featuremap_shapes[i].chlast_shape();
    }
}

void BindObjBase::convert_obj_to_numpy(const py::object &obj, py::array &arr) {
    // ensure it's a numpy array
    if (!py::isinstance<py::array>(obj)) {
        throw std::runtime_error("Input must be a numpy array");
    }

    // Convert to C-contiguous numpy array
    //
    // Ensure the input is a C-contiguous array (row-major order), similar to
    // calling `np.ascontiguousarray()` in Python.
    //
    // Note: Using `np.transpose` does not rearrange the data in memory; it only
    // returns a view with updated strides to index the elements in the new
    // order.
    //
    // Example:
    //     img = np.transpose(img, (0, 3, 1, 2))  # The result may not be
    //     C-contiguous.
    arr = py::array::ensure(obj, py::array::c_style | py::array::forcecast);

    if (!arr) {
        throw std::runtime_error(
            "Input must be convertible to a C-contiguous float32 array.");
    }
}

// Convert a raw float pointer (C-style buffer) into a NumPy array view
// without copying data. The pointer `ptr` must remain valid while Python
// is using the NumPy array.
void BindObjBase::construct_numpy_from_ptr(
    py::array_t<float> *np_array, const float *ptr,
    const std::vector<int64_t> &raw_shape) {
    // Convert shape to ssize_t (as required by pybind11)
    std::vector<ssize_t> shape(raw_shape.begin(), raw_shape.end());
    size_t ndim = shape.size();

    // Compute strides for a C-contiguous (row-major) NumPy array.
    // -----------------------------------------------------------
    // NumPy arrays need both `shape` (lengths of each dimension)
    // and `strides` (step in bytes to move along each dimension).
    //
    // Example (4D case, shape = [N, C, H, W]):
    //   strides[0] = sizeof(float) * C * H * W   // step to next batch (N)
    //   strides[1] = sizeof(float) * H * W       // step to next channel (C)
    //   strides[2] = sizeof(float) * W           // step to next row (H)
    //   strides[3] = sizeof(float)               // step to next element (W)
    //
    // The loop below generalizes this computation for any ndim.

    std::vector<ssize_t> strides(ndim);
    ssize_t stride = sizeof(float);
    for (int i = ndim - 1; i >= 0; --i) {
        strides[i] = stride;
        stride *= shape[i];
    }

    // Wrap raw float* as NumPy array (no copy, just a view)
    *np_array = py::array_t<float>(shape, strides, ptr, py::none());
}

void BindObjBase::ensure_valid_input(
    const py::array &arr, const py::buffer_info &buffer,
    const std::vector<int64_t> &expected_shape) {

    // ensure it's float32
    if (arr.dtype().kind() != 'f' || arr.dtype().itemsize() != 4) {
        throw std::runtime_error("Input array must be of type float32.");
    }

    // ensure shape matches
    std::string suffix = "Expected " + get_shape_str(expected_shape) +
                         ", got " + get_shape_str(buffer.shape) + ".";

    if (buffer.shape.size() != expected_shape.size()) {
        throw std::runtime_error(
            "Input feature map has incorrect number of dimensions. " + suffix);
    }

    for (size_t i = 0; i < expected_shape.size(); ++i) {
        if (buffer.shape[i] != expected_shape[i]) {
            throw std::runtime_error(
                "Input feature map has incorrect shape at dimension " +
                std::to_string(i) + ". " + suffix);
        }
    }
}

void BindObjBase::preallocate_buffers(
    const std::vector<std::vector<int64_t>> &shapes,
    std::vector<float *> &buffers) {
    buffers.resize(shapes.size());

    for (size_t i = 0; i < shapes.size(); i++) {
        // Calculate buffer size for this shape
        size_t buffer_len = 1;
        for (int64_t val : shapes[i]) {
            buffer_len *= val;
        }
        buffers[i] = new float[buffer_len];
    }
}

void BindObjBase::setup_shapes() {
    // Setup input shapes
    ifmap_shapes.resize(model_info.num_in_featuremaps);
    for (int i = 0; i < model_info.num_in_featuremaps; i++) {
        if (model_info.use_model_shape_in) {
            ifmap_shapes[i] = model_info.in_raw_shapes[i];
        } else {
            ifmap_shapes[i] = model_info.in_featuremap_shapes[i].chlast_shape();
        }
    }

    // Setup output shapes
    ofmap_shapes.resize(model_info.num_out_featuremaps);
for (int i = 0; i < model_info.num_out_featuremaps; i++) {
        if (model_info.use_model_shape_out) {
            ofmap_shapes[i] = model_info.out_raw_shapes[i];
        } else {
            ofmap_shapes[i] = model_info.out_featuremap_shapes[i].chlast_shape();
        }
    }
}

// =============================================================================
// BindObjMT CLASS IMPLEMENTATION
// =============================================================================

BindObjMT::BindObjMT(PyMxAcclMT *pyaccl, int model_id)
    : BindObjBase(model_id), pyaccl(pyaccl) {

auto info = pyaccl->get_model_info(model_id);

    // TODO: refactor: do not set up in constructor, but in model.start()
    setup(info);
}

void BindObjMT::connect_pre_model(const std::filesystem::path &pre_model_path,
                                  int model_id) {
    pyaccl->connect_pre_model(pre_model_path, model_id);
    auto info = pyaccl->get_pre_model_info(model_id);
    update_pre_model_info(info);
}

void BindObjMT::connect_post_model(
    const std::filesystem::path &post_model_path, int model_id,
    const std::vector<long unsigned int> &post_size_list) {
    pyaccl->connect_post_model(post_model_path, model_id, post_size_list);

    auto post_info = pyaccl->get_post_model_info(model_id);
    update_post_model_info(post_info);
}

bool BindObjMT::set_operating_frequency(int device_id, int freq){
    if(MX::Types::is_valid_frequency_option(freq)){
        MX::Types::MxFrequencyOption freq_option = static_cast<MX::Types::MxFrequencyOption>((uint16_t) (freq & 0x0000FFFF));
        return pyaccl->set_operating_frequency(device_id, freq_option);
    } else {
        return false;
    }
}

float BindObjMT::get_power(int device_id){
    return pyaccl->get_power(device_id);
}

bool BindObjMT::can_get_power_consumption(int device_id){
    return pyaccl->can_get_power_consumption(device_id);
}

float BindObjMT::get_max_temperature(int device_id){
    return pyaccl->get_max_temperature(device_id);
}

py::array_t<float> BindObjMT::get_chip_temperatures(int device_id){
    std::vector<float> temps = pyaccl->get_chip_temperatures(device_id);
    ssize_t num_chips = temps.size();
    return py::array_t<float>(num_chips, temps.data());
}

void BindObjMT::send_input(const py::object &in_data, int model_id,
                           int stream_id, int timeout) {

    // prepare input data
    std::vector<float*> ifmap_ptrs;
    prepare_input(in_data, ifmap_ptrs);

    if (!pyaccl->send_input(ifmap_ptrs, model_id, stream_id, timeout)) {
        throw std::runtime_error(
            "send_input failed: timeout after " + std::to_string(timeout) +
            " ms "
            "(model_id=" +
            std::to_string(model_id) +
            ", stream_id=" + std::to_string(stream_id) + ").");
    }
}

void BindObjMT::receive_output(std::vector<py::array_t<float>> &np_arrs,
                               int model_id, int stream_id, int timeout) {

    if (obuffer_mtxs.count(stream_id) == 0) {
        obuffer_mtxs.update(stream_id, std::make_unique<std::mutex>());
    }

    // Multi threads might call receive_output at the same time, use mutex!
    std::lock_guard<std::mutex> lock(*obuffer_mtxs[stream_id]);

    // preallocate output buffers for this stream
    if (ofmap_buffers_map.count(stream_id) == 0) {
        preallocate_buffers(ofmap_shapes, ofmap_buffers_map[stream_id]);
    }

    int num_ofmap = model_info.num_out_featuremaps;
    std::vector<float *> &ofmap_buffers = ofmap_buffers_map[stream_id];

    if (!pyaccl->receive_output(ofmap_buffers, model_id, stream_id, timeout)) {
        throw std::runtime_error(
            "receive_output failed: timeout after " + std::to_string(timeout) +
            " ms "
            "(model_id=" +
            std::to_string(model_id) +
            ", stream_id=" + std::to_string(stream_id) + ").");
    }

    np_arrs.resize(num_ofmap);

    // Convert raw output buffers to NumPy arrays
    for (int i = 0; i < num_ofmap; i++) {
        construct_numpy_from_ptr(&np_arrs[i], ofmap_buffers[i], ofmap_shapes[i]);
    }
}

void BindObjMT::run(const py::array_t<float> &in_data,
                    std::vector<py::array_t<float>> &out_data, int model_id,
                    int stream_id, int32_t timeout) {
    send_input(in_data, model_id, stream_id, timeout);
    receive_output(out_data, model_id, stream_id, timeout);
}


// =============================================================================
// BindObj CLASS IMPLEMENTATION
// =============================================================================

BindObj::BindObj(PyMxAccl *pyaccl, int model_id)
    : BindObjBase(model_id), pyaccl(pyaccl), running(true) {

    auto info = pyaccl->get_model_info(model_id);

    // TODO: refactor: do not set up in constructor, but in model.start()
    setup(info);
}

void BindObj::connect_stream(int stream_id, py::object &in_cb,
                             py::object &out_cb) {
    py_in_cb_map[stream_id] = in_cb;
    py_out_cb_map[stream_id] = out_cb;

    pyaccl->connect_stream(
        std::bind(&BindObj::in_callback, this, std::placeholders::_1,
                  std::placeholders::_2),
        std::bind(&BindObj::out_callback, this, std::placeholders::_1,
                  std::placeholders::_2),
        stream_id, model_id);

    // preallocate output buffers for this stream
    if (obuffer_mtxs.count(stream_id) == 0) {
        obuffer_mtxs.update(stream_id, std::make_unique<std::mutex>());
        preallocate_buffers(ofmap_shapes, ofmap_buffers_map[stream_id]);
    }
}

void BindObj::connect_pre_model(const std::filesystem::path &pre_model_path,
                                int model_id) {
    pyaccl->connect_pre_model(pre_model_path, model_id);
    auto info = pyaccl->get_pre_model_info(model_id);
    update_pre_model_info(info);
}

void BindObj::connect_post_model(
    const std::filesystem::path &post_model_path, int model_id,
    const std::vector<long unsigned int> &post_size_list) {
    pyaccl->connect_post_model(post_model_path, model_id, post_size_list);
    auto post_info = pyaccl->get_post_model_info(model_id);
    update_post_model_info(post_info);
}

bool BindObj::set_operating_frequency(int device_id, int freq){
    if(MX::Types::is_valid_frequency_option(freq)){
        MX::Types::MxFrequencyOption freq_option = static_cast<MX::Types::MxFrequencyOption>((uint16_t) (freq & 0x0000FFFF));
        return pyaccl->set_operating_frequency(device_id, freq_option);
    } else {
        return false;
    }
}

float BindObj::get_power(int device_id){
    return pyaccl->get_power(device_id);
}

bool BindObj::can_get_power_consumption(int device_id){
    return pyaccl->can_get_power_consumption(device_id);
}

float BindObj::get_max_temperature(int device_id){
    return pyaccl->get_max_temperature(device_id);
}

py::array_t<float> BindObj::get_chip_temperatures(int device_id){
    std::vector<float> temps = pyaccl->get_chip_temperatures(device_id);
    ssize_t num_chips = temps.size();
    return py::array_t<float>(num_chips, temps.data());
}


/* The callback can be a regular function or a generator. */
void BindObj::call_py_in_cb(py::object *obj, int stream_id) {
    py::object py_in_cb = py_in_cb_map[stream_id];
    py::object types = py::module_::import("types");
    bool is_generator = py::isinstance(py_in_cb, types.attr("GeneratorType"));

    // call the Python input callback to get the input data for this stream
    if (!is_generator) {
        *obj = py_in_cb(stream_id);
    } else {
        try {
            auto iter = py_in_cb.attr("__iter__")();
            *obj = iter.attr("__next__")();
        } catch (const py::error_already_set &e) {
            if (!e.matches(PyExc_StopIteration))
                throw std::runtime_error(e.what());
            *obj = py::none();
        }
    }
}

bool BindObj::in_callback(std::vector<const MX::Types::FeatureMap *> ifmaps,
                          int stream_id) {
    if (!running.load())
        return false;

    try {

        py::gil_scoped_acquire acquire;
        
        // Check for keyboard interrupt (SIGINT)
        if (sigint_detected.load()) {
            return false;
        }

        // Call Python user input callback
        py::object in_data;
        call_py_in_cb(&in_data, stream_id);

        // callback returns None means the stream should be closed.
        if (in_data.is_none())
            return false;

        // prepare input data
        std::vector<float*> ifmap_ptrs;
        prepare_input(in_data, ifmap_ptrs);

        py::gil_scoped_release release;

        // Copy input data from host buffers to device
        for (size_t i = 0; i < ifmaps.size(); i++) {
            ifmaps[i]->set_data(ifmap_ptrs[i]);
        }
        
        return true;
    } catch (const py::error_already_set &e) {
        spdlog::error("[mxapi] Caught py::error_already_set in in_callback: {}", e.what());
        pyaccl->eptr = std::make_exception_ptr(std::runtime_error(e.what()));
        running.store(false);
        return false;
    } catch (const std::exception &e) {
        spdlog::error("[mxapi] Caught std::exception in in_callback: {}", e.what());
        pyaccl->eptr = std::current_exception();
        running.store(false);
        return false;
    }
}

bool BindObj::out_callback(std::vector<const MX::Types::FeatureMap *> ofmaps,
                           int stream_id) {
    if (!running.load())
        return false;

    try {

        // Copy output data from device to host buffers
        std::vector<float *> ofmap_buffers;
        int num_ofmap = ofmaps.size();
        {
            std::lock_guard<std::mutex> lock(*obuffer_mtxs[stream_id]);
            ofmap_buffers = ofmap_buffers_map[stream_id]; // copy ptrs
            
            for (int i = 0; i < num_ofmap; i++) {
                ofmaps[i]->get_data(ofmap_buffers[i]);
            }
        }

        // Check for keyboard interrupt (SIGINT)
        if (sigint_detected.load()) {
            return false;
        }

        // py::array_t, py::list needs GIL
        py::gil_scoped_acquire acquire;

        std::vector<py::array_t<float>> np_arr(num_ofmap);
        py::list np_ofmaps;
        for (int i = 0; i < num_ofmap; i++) {
            construct_numpy_from_ptr(&np_arr[i], ofmap_buffers[i],
                                     ofmap_shapes[i]);
            np_ofmaps.append(np_arr[i]);
        }

        // Call the Python out callback
        py_out_cb_map[stream_id](np_ofmaps, stream_id);

        return true;
    } catch (const py::error_already_set &e) {
        spdlog::error("[mxapi] Caught py::error_already_set in out_callback: {}", e.what());
        pyaccl->eptr = std::make_exception_ptr(std::runtime_error(e.what()));
        running.store(false);
        return false;
    } catch (const std::exception &e) {
        spdlog::error("[mxapi] Caught std::exception in out_callback: {}", e.what());
        pyaccl->eptr = std::current_exception();
        running.store(false);
        return false;
    }
}

// =============================================================================
// PyMxAccl CLASS IMPLEMENTATION
// =============================================================================


PyMxAccl::PyMxAccl(const std::filesystem::path &dfp_path,
                   std::vector<int> device_ids_to_use,
                   std::array<bool, 2> use_model_shape, bool local_mode,
                   SchedulerOptions sched_options, ClientOptions client_options,
                   std::string server_addr, unsigned int server_port_base,
                   bool ignore_server)
    : MX::Runtime::MxAccl(dfp_path, device_ids_to_use, use_model_shape,
                          local_mode, sched_options, client_options,
                          server_addr, server_port_base, ignore_server) {

    int num_models = get_num_models();
    bobjs.reserve(num_models);
    for (int model_id = 0; model_id < num_models; model_id++) {
        bobjs.push_back(new BindObj(this, model_id));
    }
}

PyMxAccl::PyMxAccl(uint8_t *dfp_bytes, size_t dfp_byte_size,
                   std::vector<int> device_ids_to_use,
                   std::array<bool, 2> use_model_shape, bool local_mode,
                   SchedulerOptions sched_options, ClientOptions client_options,
                   std::string server_addr, unsigned int server_port_base,
                   bool ignore_server)
    : MX::Runtime::MxAccl(dfp_bytes, dfp_byte_size, device_ids_to_use,
                          use_model_shape, local_mode, sched_options,
                          client_options, server_addr, server_port_base,
                          ignore_server) {

    int num_models = get_num_models();
    bobjs.reserve(num_models);
    for (int model_id = 0; model_id < num_models; model_id++) {
        bobjs.push_back(new BindObj(this, model_id));
    }
}

PyMxAccl::~PyMxAccl() {
    for (auto &bobj : bobjs) {
        delete bobj;
    }
    bobjs.clear();

    // NOTE: Exit immediately on SIGINT to prevent Python shutdown issues.
    // Reason is that in pybind11, the Python callback object may be deleted
    // when the interrupted flag is set, causing child threads calling
    // it to hang, which is tricky to solve.
    if (sigint_detected.load()) {
        exit(1);
    }
}

void PyMxAccl::rethrow_exception_if_any() {
    if (eptr) {
        std::rethrow_exception(eptr);
    }
}

BindObj *PyMxAccl::get_binding_obj(int model_id) {
    int num_models = get_num_models();
    if (model_id < 0 || model_id >= num_models) {
        throw std::runtime_error(
            "Invalid model_id: " + std::to_string(model_id) +
            " (should be between 0 and " + std::to_string(num_models - 1) +
            ")");
    }
    return bobjs[model_id];
}


// =============================================================================
// PyMxAcclMT CLASS IMPLEMENTATION
// =============================================================================

PyMxAcclMT::PyMxAcclMT(const std::filesystem::path &dfp_path,
                       std::vector<int> device_ids_to_use,
                       std::array<bool, 2> use_model_shape, bool local_mode,
                       SchedulerOptions sched_options,
                       ClientOptions client_options, std::string server_addr,
                       unsigned int server_port_base, bool ignore_server)
    : MX::Runtime::MxAcclMT(dfp_path, device_ids_to_use, use_model_shape,
                            local_mode, sched_options, client_options,
                            server_addr, server_port_base, ignore_server) {

    int num_models = get_num_models();
    bobjs.reserve(num_models);
    for (int model_id = 0; model_id < num_models; model_id++) {
        bobjs.push_back(new BindObjMT(this, model_id));
    }
}

PyMxAcclMT::PyMxAcclMT(uint8_t *dfp_bytes, size_t dfp_byte_size,
                       std::vector<int> device_ids_to_use,
                       std::array<bool, 2> use_model_shape, bool local_mode,
                       SchedulerOptions sched_options,
                       ClientOptions client_options, std::string server_addr,
                       unsigned int server_port_base, bool ignore_server)
    : MX::Runtime::MxAcclMT(dfp_bytes, dfp_byte_size, device_ids_to_use,
                            use_model_shape, local_mode, sched_options,
                            client_options, server_addr, server_port_base,
                            ignore_server) {

    int num_models = get_num_models();
    bobjs.reserve(num_models);
    for (int model_id = 0; model_id < num_models; model_id++) {
        bobjs.push_back(new BindObjMT(this, model_id));
    }
}

PyMxAcclMT::~PyMxAcclMT() {
    for (auto &bobj : bobjs) {
        delete bobj;
    }
    bobjs.clear();
}

BindObjMT *PyMxAcclMT::get_binding_obj(int model_id) {
    int num_models = get_num_models();
    if (model_id < 0 || model_id >= num_models) {
        throw std::runtime_error(
            "Invalid model_id: " + std::to_string(model_id) +
            " (should be between 0 and " + std::to_string(num_models - 1) +
            ")");
    }
    return bobjs[model_id];
}

// =============================================================================
// PYBIND11 MODULE DEFINITION
// =============================================================================

/**
 * @brief Main module definition for mxapi Python bindings
 * 
 * This module provides Python bindings for the MemryX Accelerator API,
 * including both synchronous (MxAcclMT) and asynchronous (MxAccl) interfaces.
 */
PYBIND11_MODULE(mxapi, m) {

    // helper to safely call import_array(), otherwise got segfault when parsing numpy arrays
    numpy_import_array_wrapper();

    // Register raw C API functions
    PyObject *mod_ptr = m.ptr();
    PyModule_AddFunctions(mod_ptr, py_methods);

    auto default_sched = MX::RPC::SchedulerOptions();
    auto default_client = MX::RPC::ClientOptions();
    std::string default_server_addr = "/run/mxa_manager/";
    unsigned int default_server_port_base = 10000;
    bool default_ignore_server = false;
    bool default_local_mode = false;
    std::array<bool, 2> default_use_model_shape{true, true};
    std::vector<int> default_device_ids_to_use{0};

    // SchedulerOptions
    py::class_<MX::RPC::SchedulerOptions>(m, "SchedulerOptions",
        R"pbdoc(
        SchedulerOptions encapsulates various options for the scheduler, including frame and time limits, queue sizes, and autoclock settings.
        )pbdoc")
        .def(py::init<>()) // default constructor
        .def(py::init<int, int, int, int, bool, int, bool, int, int>(),
            py::arg("frame_limit") = default_sched.frame_limit,
            py::arg("time_limit") = default_sched.time_limit,
            py::arg("ifmap_queue_size") = default_sched.ifmap_queue_size,
            py::arg("ofmap_queue_size") = default_sched.ofmap_queue_size,
            py::arg("autoclock_enabled") = default_sched.autoclock_enabled,
            py::arg("autoclock_power_limit_mw") = default_sched.autoclock_power_limit_mw,
            py::arg("autoclock_check_fps_saturation") = default_sched.autoclock_check_fps_saturation,
            py::arg("autoclock_sample_interval_ms") = default_sched.autoclock_sample_interval_ms,
            py::arg("autoclock_num_samples") = default_sched.autoclock_num_samples
        )
        .def_readwrite("frame_limit", &MX::RPC::SchedulerOptions::frame_limit,
            R"pbdoc(
            The number of frames to process before the DFP is rescheduled. Default: 20
            )pbdoc")
        .def_readwrite("time_limit", &MX::RPC::SchedulerOptions::time_limit,
            R"pbdoc(
            The maximum idle time (in milliseconds) to wait for new input before forcing the DFP to be rescheduled. Default: 250 ms
            )pbdoc")
        .def_readwrite("ifmap_queue_size", &MX::RPC::SchedulerOptions::ifmap_queue_size,
            R"pbdoc(
            Capacity of the input feature map (ifmap) queue used by the DFP, shared by all clients. Default: 16
            )pbdoc")
        .def_readwrite("ofmap_queue_size", &MX::RPC::SchedulerOptions::ofmap_queue_size,
            R"pbdoc(
            Capacity of the **per-client** output feature map (ofmap) queues. Default: 21 (frame_limit + 1)
            )pbdoc")
        .def_readwrite("autoclock_enabled", &MX::RPC::SchedulerOptions::autoclock_enabled,
            R"pbdoc(
            Enable automatic frequency up/down-clocking when starting DFP, **if supposed by the target MX3 hardware**. Default: false
            )pbdoc")
        .def_readwrite("autoclock_power_limit_mw", &MX::RPC::SchedulerOptions::autoclock_power_limit_mw,
            R"pbdoc(
            Power limit in milliwatts. Autoclocking will increase frequency until this power limit is reached. Range: [1000, 14700]. Default: 11500 mW
            )pbdoc")
        .def_readwrite("autoclock_check_fps_saturation", &MX::RPC::SchedulerOptions::autoclock_check_fps_saturation,
            R"pbdoc(
            **Experimental:** If true, autoclocking will stop increasing frequency, even if there's still power headroom, if FPS has not increased over the last 3 frequency steps. Default: false
            )pbdoc")
        .def_readwrite("autoclock_sample_interval_ms", &MX::RPC::SchedulerOptions::autoclock_sample_interval_ms,
            R"pbdoc(
            Interval in milliseconds between power samples while testing autoclocking steps. Default: 50 ms
            )pbdoc")
        .def_readwrite("autoclock_num_samples", &MX::RPC::SchedulerOptions::autoclock_num_samples,
            R"pbdoc(
            Number of power samples to collect (and find the maximum of) before making a decision to stay at the current frequency or increase it. Default: 6
            )pbdoc");
    
    // ClientOptions
    py::class_<MX::RPC::ClientOptions>(m, "ClientOptions",
        R"pbdoc(
        Configures client-side execution behavior, such as FPS smoothing.
        )pbdoc")
        .def(py::init<>()) // default constructor
        .def(py::init<bool, float>(),
            py::arg("smoothing") = default_client.smoothing,
            py::arg("fps_target") = default_client.fps_target
        )
        .def_readwrite("smoothing", &MX::RPC::ClientOptions::smoothing,
            R"pbdoc(
            If true, enables frame rate smoothing to avoid sudden FPS "jumps", at the cost of increased in-to-out latency. Default: false
            )pbdoc")
        .def_readwrite("fps_target", &MX::RPC::ClientOptions::fps_target,
            R"pbdoc(
            When smoothing is enabled, the target FPS to smooth towards. mxa-manager will automatically adjust the delay between frames to try to achieve this target FPS.
            )pbdoc");

    // DfpObject
    py::class_<Dfp::DfpObject>(m, "DfpObject")
        .def(py::init<std::string>(),
                py::arg("filename"))
        .def_readonly("dfp_byte_size", &Dfp::DfpObject::dfp_byte_size)
        .def(
            "get_src_dfp_bytes",
            [](Dfp::DfpObject &self, size_t length) {
                return py::bytes(
                    reinterpret_cast<const char *>(self.src_dfp_bytes), length);
            },
            py::arg("length"), "Returns DFP bytes as Python bytes")
        .def_property_readonly("dfp_version_str",
                               [](Dfp::DfpObject &self) {
                                   return self.get_dfp_meta()->dfp_version_str;
                               })
        .def_property_readonly("dfp_version",
                               [](Dfp::DfpObject &self) {
                                   return self.get_dfp_meta()->dfp_version;
                               })
        .def_property_readonly("compile_time",
                               [](Dfp::DfpObject &self) {
                                   return self.get_dfp_meta()->compile_time;
                               })
        .def_property_readonly("compiler_version",
                               [](Dfp::DfpObject &self) {
                                   return self.get_dfp_meta()->compiler_version;
                               })
        .def_property_readonly(
            "chip_gen",
            [](Dfp::DfpObject &self) { return self.get_dfp_meta()->mxa_gen; })
        .def_property_readonly("chip_gen_name",
                               [](Dfp::DfpObject &self) {
                                   return self.get_dfp_meta()->mxa_gen_name;
                               })
        .def_property_readonly(
            "num_chips",
            [](Dfp::DfpObject &self) { return self.get_dfp_meta()->num_chips; })
        .def_property_readonly(
            "use_multigroup_lb",
            [](Dfp::DfpObject &self) {
                return self.get_dfp_meta()->use_multigroup_lb;
            })
        .def_property_readonly("num_inports",
                               [](Dfp::DfpObject &self) {
                                   return self.get_dfp_meta()->num_inports;
                               })
        .def_property_readonly("num_outports",
                               [](Dfp::DfpObject &self) {
                                   return self.get_dfp_meta()->num_outports;
                               })
        .def_property_readonly("num_used_inports",
                               [](Dfp::DfpObject &self) {
                                   return self.get_dfp_meta()->num_used_inports;
                               })
        .def_property_readonly(
            "num_used_outports",
            [](Dfp::DfpObject &self) {
                return self.get_dfp_meta()->num_used_outports;
            })
        .def_property_readonly("num_models",
                               [](Dfp::DfpObject &self) {
                                   return self.get_dfp_meta()->num_models;
                               })
        .def_property_readonly("model_inports",
                               [](Dfp::DfpObject &self) {
                                   return self.get_dfp_meta()->model_inports;
                               })
        .def_property_readonly("model_outports", [](Dfp::DfpObject &self) {
            return self.get_dfp_meta()->model_outports;
        });

    //  Device Info device_info_t
    py::class_<MX::RPC::device_info_t>(m, "device_info_t",
        R"pbdoc(
        Struct with info about a MemryX device, retrieved from mxa-manager.
        )pbdoc")
        .def(py::init<int32_t, int32_t, int32_t, int32_t, bool, bool, std::vector<uint16_t>, uint16_t>())
        .def_readwrite("chip_count", 
                &MX::RPC::device_info_t::chip_count,
                R"pbdoc(
                Number of chips in the device.
                )pbdoc")
        .def_readwrite("current_config",
                &MX::RPC::device_info_t::current_config,
                R"pbdoc(
                **DEPRECATED** Current 'group' configuration of the device.
                )pbdoc")
        .def_readwrite("num_groups", 
                &MX::RPC::device_info_t::num_groups,
                R"pbdoc(
                **DEPRECATED** Number of 'groups' in the device.
                )pbdoc")
        .def_readwrite("chips_per_group",
                &MX::RPC::device_info_t::chips_per_group,
                R"pbdoc(
                **DEPRECATED** Number of chips per group.
                )pbdoc")
        .def_readwrite("can_get_power_data",
                &MX::RPC::device_info_t::can_get_power_data,
                R"pbdoc(
                Whether power data can be retrieved from the device. If false, power will always be reported as -1.
                )pbdoc")
        .def_readwrite("is_usb",
                &MX::RPC::device_info_t::is_usb,
                R"pbdoc(
                Whether the device is connected via USB.
                )pbdoc")
        .def_readwrite("freqs",
                &MX::RPC::device_info_t::freqs,
                R"pbdoc(
                Frequencies in MHz of each chip on the device. The length of this list should match `chip_count`.
                )pbdoc")
        .def_readwrite("volt",
                &MX::RPC::device_info_t::volt,
                R"pbdoc(
                Voltage in mV of the device. This applies to all chips on the device.
                )pbdoc")
        .def("__getitem__",
             [](const MX::RPC::device_info_t &self,
                const std::string &key) -> py::object {
                 if (key == "chip_count")
                     return py::cast(self.chip_count);
                 if (key == "current_config")
                     return py::cast(self.current_config);
                 if (key == "num_groups")
                     return py::cast(self.num_groups);
                 if (key == "chips_per_group")
                     return py::cast(self.chips_per_group);
                 if (key == "can_get_power_data")
                     return py::cast(self.can_get_power_data);
                 if (key == "freqs")
                     return py::cast(self.freqs);
                 if (key == "volt")
                     return py::cast(self.volt);
                 throw std::out_of_range("Invalid key in device_info_t: " +
                                         key);
             })
        .def("__contains__",
             [](const MX::RPC::device_info_t &, const std::string &key) {
                 return key == "chip_count" || key == "current_config" ||
                        key == "num_groups" || key == "chips_per_group" ||
                        key == "can_get_power_data" || key == "freqs" ||
                        key == "volt";
             })
        .def("keys", []() {
            return std::vector<std::string>{"chip_count",
                                            "current_config",
                                            "num_groups",
                                            "chips_per_group",
                                            "can_get_power_data",
                                            "freqs",
                                            "volt"};
        });

    // Client
    py::class_<MX::Runtime::Client>(m, "Client",
        R"pbdoc(
        Python binding of the MX::Runtime::Client class, which can be used to query information from mxa-manager (such as power, temperature, pressure, etc.) separately from an MxAccl/MxAcclMT instance.
        )pbdoc")
        .def(py::init<>())
        .def("init_connection", &MX::Runtime::Client::init_connection,
             py::arg("server_address") = "/run/mxa_manager/",
             py::arg("base_port") = 10000,
             R"pbdoc(
             Initializes connection to mxa-manager. Must be called before any other Client method.

             Parameters
             ----------

                server_address : str, optional
                    Address of the mxa-manager server (default: "/run/mxa_manager/"). For local UNIX socket connection, use the format "/path/to/socket". For TCP connection, use the format "ip_address".

                base_port : int, optional
                    Base port number (TCP) or numeric filename (UNIX) for connecting to mxa-manager (default: 10000).

             Returns
             -------
    
                bool
                    True if connection is successfully established, False otherwise.
            )pbdoc")
        .def("end_connection", &MX::Runtime::Client::end_connection,
            R"pbdoc(
            Ends connection to mxa-manager. Should be called to clean up resources when Client is no longer needed.

            Returns
            -------

                bool
                    True if connection is successfully closed, False otherwise.
            )pbdoc")
        .def("get_avg_max_temp", &MX::Runtime::Client::get_avg_max_temp,
            py::arg("device_id"),
            R"pbdoc(
            Gets the rolling **average** of the maximum temperatures across all chips on the device.

            Parameters
            ----------

                device_id : int
                    ID of the device to query.

            Returns
            -------

                float
                    Average of the maximum temperatures across all chips on the device, in degrees Celsius, over the past HW_MONITOR_INFO (set in `/etc/memryx/mxa_manager.conf`, default 500ms).

            )pbdoc")
        .def("get_inst_max_temp", &MX::Runtime::Client::get_inst_max_temp,
            py::arg("device_id"),
            R"pbdoc(
            Gets the current maximum temperature across all chips on the device.

            Parameters
            ----------

                device_id : int
                    ID of the device to query.

            Returns
            -------

                float
                    Current maximum temperature across all chips on the device, in degrees Celsius.

            )pbdoc")
        .def("get_avg_power", &MX::Runtime::Client::get_avg_power,
            py::arg("device_id"),
            R"pbdoc(
            Gets the rolling average power consumption of the device, if supported by the device.

            Parameters
            ----------

                device_id : int
                    ID of the device to query.

            Returns
            -------

                float
                    Rolling average power consumption of the device in milliwatts (mW) over the past HW_MONITOR_INFO (set in `/etc/memryx/mxa_manager.conf`, default 500ms), if supported by the device. If not supported, returns -1.

            )pbdoc")
        .def("get_inst_power", &MX::Runtime::Client::get_inst_power,
            py::arg("device_id"),
            R"pbdoc(
            Gets the current power consumption of the device, if supported by the device.

            Parameters
            ----------

                device_id : int
                    ID of the device to query.

            Returns
            -------

                float
                    Current power consumption of the device in milliwatts (mW), if supported by the device. If not supported, returns -1.
            )pbdoc")
        .def("set_power_mode", &MX::Runtime::Client::set_power_mode,
            py::arg("device_id"),
            py::arg("freq_mhz"),
            R"pbdoc(
            Sets the operating frequency of the specified device.

            Parameters
            ----------

                device_id : int
                    ID of the device to set the frequency on.

                freq_mhz : int
                    Desired operating frequency in MHz. Must be a valid 25MHz step between 200 and 1000.

            Returns
            -------

                bool
                    True if the frequency was successfully set, False otherwise (e.g. invalid frequency or device_id).
            )pbdoc")
        .def("get_pressure", [](
                // wrapper to return pressure as string "low", "medium", "high", "full"
                // instead of a float number
                MX::Runtime::Client& self,
                int32_t device_id) {
                    float pressure = self.get_pressure(device_id);
                    if (pressure < MEMX_PRESSURE_LOW_THRESH) {
                        return std::string("low");
                    } else if (pressure < MEMX_PRESSURE_MEDIUM_THRESH) {
                        return std::string("medium");
                    } else if (pressure < MEMX_PRESSURE_HIGH_THRESH) {
                        return std::string("high");
                    } else {
                        return std::string("full");
                    }
            },
            py::arg("device_id"),
            R"pbdoc(
            Gets the current "pressure" level of the device, hinting to the user how many more streams could be assigned to the device before it becomes overloaded.

            Parameters
            ----------

                device_id : int
                    ID of the device to query.

            Returns
            -------

                str
                    Current pressure level of the device, returned as a string with possible values: "low", "medium", "high", "full". See C++ API docs for details on what these levels mean.

            )pbdoc")
        .def("try_local_lock", &MX::Runtime::Client::try_local_lock,
             py::arg("device_id"))
        .def("local_unlock", &MX::Runtime::Client::local_unlock,
             py::arg("device_id"))
        .def("get_device_infos", &MX::Runtime::Client::get_device_infos,
            R"pbdoc(
            Gets information about all available MemryX devices from mxa-manager.

            Returns
            -------

                List[device_info_t]
                    A list of device_info_t objects, each containing info about that MXA device.
            )pbdoc")
        .def(
            "connect_dfp",
            [](MX::Runtime::Client &self, py::bytes dfp_bytes, int32_t model_id,
               const SchedulerOptions &sched_options,
               const ClientOptions &client_options,
               std::vector<int32_t> devices_to_use) {
                auto info = py::buffer(dfp_bytes).request();
                return self.connect_dfp(
                    info.size, static_cast<uint8_t *>(info.ptr), model_id,
                    sched_options, client_options,
                    static_cast<int32_t>(devices_to_use.size()),
                    devices_to_use.data());
            },
            py::arg("dfp_bytes"), 
            py::arg("model_id"),
            py::arg("scheduler_options"), 
            py::arg("client_options"),
            py::arg("devices_to_use"))
        .def_readwrite("my_client_id", &MX::Runtime::Client::my_client_id,
            R"pbdoc(
            The unique client ID assigned by mxa-manager to this Client instance after a successful call to `init_connection`.
            )pbdoc");

    // MxAccl
    py::class_<PyMxAccl>(m, "MxAccl",
        R"pbdoc(
        Python binding of the MxAccl class (auto-threading)
        )pbdoc")
        // Constructor: from dfp path
        .def(py::init<const std::filesystem::path &, std::vector<int>,
                      std::array<bool, 2>, bool, SchedulerOptions,
                      ClientOptions, std::string, unsigned int, bool>(),
             py::arg("dfp_path"),
             py::arg("device_ids_to_use") = default_device_ids_to_use,
             py::arg("use_model_shape") = default_use_model_shape,
             py::arg("local_mode") = default_local_mode,
             py::arg("sched_options") = MX::RPC::SchedulerOptions(),
             py::arg("client_options") = MX::RPC::ClientOptions(),
             py::arg("server_addr") = default_server_addr,
             py::arg("server_port_base") = default_server_port_base,
             py::arg("ignore_server") = default_ignore_server)
        // Constructor: from dfp bytes
        .def(
            py::init([](py::bytes dfp_bytes, size_t dfp_bytes_size,
                        std::vector<int> device_ids_to_use,
                        std::array<bool, 2> use_model_shape, bool local_mode,
                        SchedulerOptions sched_options,
                        ClientOptions client_options, std::string server_addr,
                        unsigned int server_port_base, bool ignore_server) {

                // Convert py::bytes -> std::string
                std::string buffer = dfp_bytes;
                uint8_t *data_ptr = reinterpret_cast<uint8_t *>(buffer.data());
                // size_t size = buffer.size();

                PyMxAccl *pyaccl = new PyMxAccl(
                    data_ptr, dfp_bytes_size, device_ids_to_use,
                    use_model_shape, local_mode, sched_options,
                    client_options, server_addr, server_port_base,
                    ignore_server);

                return pyaccl;
            }),
            py::arg("dfp_bytes"), py::arg("dfp_bytes_size"),
            py::arg("device_ids_to_use") = default_device_ids_to_use,
            py::arg("use_model_shape") = default_use_model_shape,
            py::arg("local_mode") = default_local_mode,
            py::arg("sched_options") = MX::RPC::SchedulerOptions(),
            py::arg("client_options") = MX::RPC::ClientOptions(),
            py::arg("server_addr") = default_server_addr,
            py::arg("server_port_base") = default_server_port_base,
            py::arg("ignore_server_") = default_ignore_server,
            py::doc(R"pbdoc(
            Initializes the MxAccl object by loading a DFP from a file (1st version) or from raw bytes (2nd version).

            Parameters
            ----------

                dfp_path : str
                  Path to the DFP file.

                dfp_bytes : bytes
                  DFP as raw bytes object.

                dfp_bytes_size : int
                  Size of the DFP bytes.

                device_ids_to_use : List[int], optional
                  List of device IDs to use. Default is [0].

                use_model_shape : Tuple[bool, bool], optional
                  Whether to use model shape for input and output feature maps.
                  Default is (True, True).

                local_mode : bool, optional
                  Whether to run in local mode without connecting to server.
                  Default is False.

                sched_options : SchedulerOptions, optional
                  Scheduler options for this client. Default is SchedulerOptions().

                client_options : ClientOptions, optional
                  Client options for this client. Default is ClientOptions().

                server_addr : str, optional
                  Server address to connect to (e.g., "/run/mxa_manager/").
                  Default is "/run/mxa_manager/".

                server_port_base : int, optional
                  Base port number for server connection. Default is 10000.

                ignore_server : bool, optional
                  Whether to ignore server connection and run without it (*DANGER*).
                  Default is False.
            )pbdoc")) 
        .def(
            "connect_post_model",
            [](PyMxAccl &self, std::filesystem::path &post_model_path,
               int model_id, const std::vector<size_t> &post_size_list) {

                self.get_binding_obj(model_id)->connect_post_model(
                    post_model_path, model_id, post_size_list);
            },
            py::arg("post_model_path"), py::arg("model_id") = 0,
            py::arg("post_size_list") = std::vector<size_t>{},
            py::doc(R"pbdoc(
                Connects a post-processing model to the specified model ID.

                Parameters
                ----------

                    post_model_path : str
                      The file path to the post-processing model.

                    model_id : int, optional
                      The ID of the model to connect to. Default is 0.

            )pbdoc"))
        .def(
            "connect_pre_model",
            [](PyMxAccl &self, std::filesystem::path &pre_model_path,
               int model_id) {

                self.get_binding_obj(model_id)->connect_pre_model(
                    pre_model_path, model_id);
            },
            py::arg("pre_model_path"), py::arg("model_id") = 0,
            py::doc(R"pbdoc(
                Connects a pre-processing model to the specified model ID.

                Parameters
                ----------

                    pre_model_path : str
                      The file path to the pre-processing model.

                    model_id : int, optional
                      The ID of the model to connect to. Default is 0.
            )pbdoc"))
        .def(
            "connect_stream",
            [](PyMxAccl &self, py::object &in_callback,
               py::object &out_callback, int stream_id, int model_id) {

                self.get_binding_obj(model_id)->connect_stream(
                    stream_id, in_callback, out_callback);
            },
            py::arg("in_callback"), py::arg("out_callback"),
            py::arg("stream_id"), py::arg("model_id") = 0,
            R"pbdoc(
            Connects a stream to a model using the specified input and output callbacks functions.

            This method registers a data stream for the given model and binds it to both input and
            output callback functions. Streams uniquely identified by `stream_id` can be connected to the same model.
            This function must be called before `start()`.

            The provided input callback function should have the following signature, where the return value is either a list of numpy arrays or None.

            ```python
            def in_callback(stream_id: int) -> List[np.ndarray]:
            ```


            And the output callback function should have the following signature:

            ```python
            def out_callback(ofmaps: List[np.ndarray], stream_id: int) -> None:
            ```

            Parameters
            ----------

                in_callback : Callable[[int], Optional[List[np.ndarray]]]
                  A Python callable that takes a stream ID as input and returns a list of numpy arrays to the MXA, or None to indicate the end of the stream.

                out_callback : Callable[[List[np.ndarray], int], None]
                  A Python callable that takes a list of numpy arrays (ofmaps from the MXA) and a stream ID as input.

                stream_id : int
                  An integer identifier for the stream. This can be used to differentiate multiple streams connected to the same model.

                model_id : int, optional
                  The ID of the model to connect the stream to. Default is 0.

            )pbdoc")
        .def(
            "set_operating_frequency",
            [](PyMxAccl &self, int device_id, int freq) {
                return self.get_binding_obj(0)->set_operating_frequency(device_id, freq);
            },
            py::arg("device_id"), py::arg("freq"),
            R"pbdoc(
            Sets the operating frequency of the specified device.

            Parameters
            ----------

                device_id : int
                  The ID of the device to set the frequency for.

                freq : int
                  The desired operating frequency in MHz. Must be a valid 25MHz step between 200 and 1000.


            Returns
            -------

                bool
                  True if the frequency was successfully set, False otherwise.

            )pbdoc")
        .def(
            "get_power",
            [](PyMxAccl &self, int device_id) {
                return self.get_binding_obj(0)->get_power(device_id);
            },
            py::arg("device_id"),
            R"pbdoc(
            Gets the current power consumption of the specified device in mW.

            Parameters
            ----------

                device_id : int
                  The ID of the device to query.

            Returns
            -------

                float
                  The current power consumption in milliwatts (mW). Returns -1 if power data is unavailable.
            )pbdoc")
        .def(
            "can_get_power_consumption",
            [](PyMxAccl &self, int device_id) {
                return self.get_binding_obj(0)->can_get_power_consumption(device_id);
            },
            py::arg("device_id"),
            R"pbdoc(
            Checks if the given device is able to report power consumption data.

            Parameters
            ----------

                device_id : int
                  The ID of the device to check.

            Returns
            -------

                bool
                  True if power consumption data can be retrieved for the device, False otherwise.
            )pbdoc")
        .def("get_max_temperature",
            [](PyMxAccl &self, int device_id) {
                return self.get_binding_obj(0)->get_max_temperature(device_id);
            },
            py::arg("device_id"),
            R"pbdoc(
            Gets the current maximum temperature among all chips on the specified device in Celsius.

            Parameters
            ----------

                device_id : int
                  The ID of the device to query.

            Returns
            -------

                float
                  The current maximum temperature in Celsius. Returns -1 if temperature data is unavailable.
            )pbdoc")
        .def("get_chip_temperatures",
            [](PyMxAccl &self, int device_id) {
                return self.get_binding_obj(0)->get_chip_temperatures(device_id);
            },
            py::arg("device_id"),
            R"pbdoc(
            Gets the current temperature of each chip on the specified device in Celsius.

            Parameters
            ----------

                device_id : int
                  The ID of the device to query.

            Returns
            -------

                List[float]
                  A list of current temperatures in Celsius for each chip on the device.
            )pbdoc")
        .def(
            "start",
            [](PyMxAccl &self, int model_id) {
                // Need to release GIL in main thread!
                py::gil_scoped_release release;
                self.start(model_id);
            },
            py::arg("model_id") = -1,
            R"pbdoc(
            Starts the execution of the specified model, or all models in the DFP if model_id is -1.

            Parameters
            ----------

                model_id : int, optional
                  The ID of the model to start. If -1, starts all models in the DFP. Default is -1.

            )pbdoc")
        .def(
            "wait",
            [](PyMxAccl &self, int model_id) {

                // Check for SIGINT periodically while waiting
                // Detail: https://github.com/memryx/MX_API/pull/252#issuecomment-3442787703
                while (true) {
                    
                    if (self.all_tasks_done(model_id)) {
                        // model finished all stream stasks, so break
                        break;
                    }

                    // Check for Python signals (e.g., SIGINT)
                    // PyErr_CheckSignals acquires the GIL automatically
                    if (PyErr_CheckSignals() != 0) {
                        sigint_detected.store(true);
                        throw py::error_already_set();
                    }
                    py::gil_scoped_release release;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
                py::gil_scoped_release release;

                // If there was an exception in the callback, rethrow it here
                // Rationale: Throwing exceptions directly inside callback functions,
                // pybind11 C++ program crashes somehow.
                self.rethrow_exception_if_any();

                // Finally, call wait to clean up
                self.wait(model_id);
            },
            py::arg("model_id") = -1,
            R"pbdoc(
            Blocks until all streams for the specified model(s) have completed execution.

            Parameters
            ----------

                model_id : int, optional
                  The ID of the model to wait for. If -1, waits for all models in the DFP. Default is -1.

            )pbdoc")
        .def(
            "stop",
            [](PyMxAccl &self, int model_id) {
                // Do nothing. Keep this function just for backward compatibility.
            },
            py::arg("model_id") = -1,
            R"pbdoc(
            Stops the execution of the specified model, or all models in the DFP if model_id is -1.

            Parameters
            ----------

                model_id : int, optional
                  The ID of the model to stop. If -1, stops all models in the DFP. Default is -1.
            )pbdoc")
        .def(
            "get_raw_ptr",
            [](PyMxAccl &self) {
                // Return the memory address as a uintptr_t, other C++ files can use reinterpret_cast to convert it back
                return reinterpret_cast<uintptr_t>(&self);
            }
        );

    // MxAcclMT
    py::class_<PyMxAcclMT>(m, "MxAcclMT",
        R"pbdoc(
        Python binding of the MxAcclMT class (manual-threading)
        )pbdoc")
        // Constructor: from dfp path
        .def(py::init<const std::filesystem::path &, std::vector<int>,
                      std::array<bool, 2>, bool, SchedulerOptions,
                      ClientOptions, std::string, unsigned int, bool>(),
             py::arg("dfp_path"),
             py::arg("device_ids_to_use") = default_device_ids_to_use,
             py::arg("use_model_shape") = default_use_model_shape,
             py::arg("local_mode") = default_local_mode,
             py::arg("sched_options") = MX::RPC::SchedulerOptions(),
             py::arg("client_options") = MX::RPC::ClientOptions(),
             py::arg("server_addr") = default_server_addr,
             py::arg("server_port_base") = default_server_port_base,
             py::arg("ignore_server") = default_ignore_server)
        // Constructor: from dfp bytes
        // Constructor: from dfp bytes
        .def(
            py::init([](py::bytes dfp_bytes, size_t dfp_bytes_size,
                        std::vector<int> device_ids_to_use,
                        std::array<bool, 2> use_model_shape, bool local_mode,
                        SchedulerOptions sched_options,
                        ClientOptions client_options, std::string
                        server_addr, unsigned int server_port_base, bool
                        ignore_server) {

                // Convert py::bytes -> std::string
                std::string buffer = dfp_bytes;
                uint8_t *data_ptr = reinterpret_cast<uint8_t *>(buffer.data());
                // size_t size = buffer.size();

                PyMxAcclMT *pyaccl = new PyMxAcclMT(
                    data_ptr, dfp_bytes_size, device_ids_to_use,
                    use_model_shape, local_mode, sched_options,
                    client_options, server_addr, server_port_base,
                    ignore_server);

                return pyaccl;
            }),
            py::arg("dfp_bytes"),
            py::arg("dfp_bytes_size"),
            py::arg("device_ids_to_use") = default_device_ids_to_use,
            py::arg("use_model_shape") = default_use_model_shape,
            py::arg("local_mode") = default_local_mode,
            py::arg("sched_options") = MX::RPC::SchedulerOptions(),
            py::arg("client_options") = MX::RPC::ClientOptions(),
            py::arg("server_addr") = default_server_addr,
            py::arg("server_port_base") = default_server_port_base,
            py::arg("ignore_server_") = default_ignore_server)
        .def(
            "connect_post_model",
            [](PyMxAcclMT &self, std::filesystem::path &post_model_path,
               int model_id, const std::vector<size_t> &post_size_list) {

                self.get_binding_obj(model_id)->connect_post_model(
                    post_model_path, model_id, post_size_list);
            },
            py::arg("post_model_path"), py::arg("model_id") = 0,
            py::arg("post_size_list") = std::vector<size_t>{},
            py::doc(R"pbdoc(
                Connects a post-processing model to the specified model ID.

                Parameters
                ----------

                    post_model_path : str
                      The file path to the post-processing model.

                    model_id : int, optional
                      The ID of the model to connect to. Default is 0.

            )pbdoc"))
        .def(
            "connect_pre_model",
            [](PyMxAcclMT &self, std::filesystem::path &pre_model_path,
               int model_id) {

                self.get_binding_obj(model_id)->connect_pre_model(
                    pre_model_path, model_id);
            },
            py::arg("pre_model_path"), py::arg("model_id") = 0,
            py::doc(R"pbdoc(
                Connects a pre-processing model to the specified model ID.

                Parameters
                ----------

                    pre_model_path : str
                      The file path to the pre-processing model.

                    model_id : int, optional
                      The ID of the model to connect to. Default is 0.
            )pbdoc"))
        .def(
            "set_operating_frequency",
            [](PyMxAcclMT &self, int device_id, int freq) {
                return self.get_binding_obj(0)->set_operating_frequency(device_id, freq);
            },
            py::arg("device_id"), py::arg("freq"),
            R"pbdoc(
            Sets the operating frequency of the specified device.

            Parameters
            ----------

                device_id : int
                  The ID of the device to set the frequency for.

                freq : int
                  The desired operating frequency in MHz. Must be a valid 25MHz step between 200 and 1000.


            Returns
            -------

                bool
                  True if the frequency was successfully set, False otherwise.

            )pbdoc")
        .def(
            "get_power",
            [](PyMxAcclMT &self, int device_id) {
                return self.get_binding_obj(0)->get_power(device_id);
            },
            py::arg("device_id"),
            R"pbdoc(
            Gets the current power consumption of the specified device in mW.

            Parameters
            ----------

                device_id : int
                  The ID of the device to query.

            Returns
            -------

                float
                  The current power consumption in milliwatts (mW). Returns -1 if power data is unavailable.
            )pbdoc")
        .def(
            "can_get_power_consumption",
            [](PyMxAcclMT &self, int device_id) {
                return self.get_binding_obj(0)->can_get_power_consumption(device_id);
            },
            py::arg("device_id"),
            R"pbdoc(
            Checks if the given device is able to report power consumption data.

            Parameters
            ----------

                device_id : int
                  The ID of the device to check.

            Returns
            -------

                bool
                  True if power consumption data can be retrieved for the device, False otherwise.
            )pbdoc")
        .def("get_max_temperature",
            [](PyMxAcclMT &self, int device_id) {
                return self.get_binding_obj(0)->get_max_temperature(device_id);
            },
            py::arg("device_id"),
            R"pbdoc(
            Gets the current maximum temperature among all chips on the specified device in Celsius.

            Parameters
            ----------

                device_id : int
                  The ID of the device to query.

            Returns
            -------

                float
                  The current maximum temperature in Celsius. Returns -1 if temperature data is unavailable.
            )pbdoc")
        .def("get_chip_temperatures",
            [](PyMxAcclMT &self, int device_id) {
                return self.get_binding_obj(0)->get_chip_temperatures(device_id);
            },
            py::arg("device_id"),
            R"pbdoc(
            Gets the current temperature of each chip on the specified device in Celsius.

            Parameters
            ----------

                device_id : int
                  The ID of the device to query.

            Returns
            -------

                List[float]
                  A list of current temperatures in Celsius for each chip on the device.
            )pbdoc")
        .def(
            "send_input",
            [](PyMxAcclMT &self, const py::object &in_data, int model_id,
               int stream_id, int32_t timeout) {

                self.get_binding_obj(model_id)->send_input(in_data, model_id,
                                                           stream_id, timeout);
            },
            py::arg("in_data"), py::arg("model_id"), py::arg("stream_id"),
            py::arg("timeout") = 0,
            R"pbdoc(
            Sends input data to the accelerator for the specified model and stream.

            Parameters
            ----------

                in_data : List[np.ndarray] or np.ndarray
                  The input feature maps to send.

                model_id : int
                  The ID of the model to send the input to.

                stream_id : int
                  The ID of the stream to send the input to.

                timeout : int, optional
                  The maximum time to wait for the input to be sent, in milliseconds. Default is 0 (no timeout).

            Returns
            -------

                bool
                  True if the input was successfully sent, False if there was a timeout or error.
            )pbdoc")
        .def(
            "receive_output",
            [](PyMxAcclMT &self, int model_id, int stream_id, int32_t timeout) {

                std::vector<py::array_t<float>> out_data;
                self.get_binding_obj(model_id)->receive_output(
                    out_data, model_id, stream_id, timeout);

                return out_data;
            },
            py::arg("model_id"), py::arg("stream_id"), py::arg("timeout") = 0,
            R"pbdoc(
            Receives output data from the accelerator for the specified model and stream.

            Parameters
            ----------

                model_id : int
                  The ID of the model to receive the output from.

                stream_id : int
                  The ID of the stream to receive the output from.

                timeout : int, optional
                  The maximum time to wait for the output to be received, in milliseconds. Default is 0 (no timeout).

            Returns
            -------

                List[np.ndarray]
                  The output feature maps received from the accelerator. Returns None if there was a timeout or error.
            )pbdoc")
        .def(
            "run",
            [](PyMxAcclMT &self, const py::array_t<float> &in_data,
               int model_id, int stream_id, int32_t timeout) {

                std::vector<py::array_t<float>> out_data;
                self.get_binding_obj(model_id)->run(in_data, out_data, model_id,
                                                    stream_id, timeout);

                return out_data;
            },
            py::arg("in_data"), py::arg("model_id"), py::arg("stream_id"),
            py::arg("timeout") = 0,
            R"pbdoc(
            Runs inference on a batch of inputs and returns the resulting the outputs.

            **WARNING!** While convenient for simple use cases, this method has much worse performance than using `send_input` and `receive_output` in separate threads. Separate threads are necessary to fully utilize the MXA's pipelined architecture. Real-time applications should never use this function.

            Parameters
            ----------

                in_data : List[np.ndarray] or np.ndarray
                  The input feature maps to send.

                model_id : int
                  The ID of the model to run inference on.

                stream_id : int
                  The ID of the stream to run inference on.

                timeout : int, optional
                  The maximum time to wait for the inference to complete, in milliseconds. Default is 0 (no timeout).

            Returns
            -------

                List[np.ndarray]
                  The output feature maps received from the accelerator. Returns None if there was a timeout or error.

            )pbdoc");

    // FeatureMap
    py::class_<MX::Types::FeatureMap>(m, "FeatureMap");
    // expose the MEMX_PRESSURE_* constants
    m.attr("MEMX_PRESSURE_LOW_THRESH") = MEMX_PRESSURE_LOW_THRESH;
    m.attr("MEMX_PRESSURE_MEDIUM_THRESH") = MEMX_PRESSURE_MEDIUM_THRESH;
    m.attr("MEMX_PRESSURE_HIGH_THRESH") = MEMX_PRESSURE_HIGH_THRESH;
}
