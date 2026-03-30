/***************************************************************************//**
 * @note
 * Copyright (C) 2019-2026 MemryX
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 ******************************************************************************/
#ifndef MEMX_H_
#define MEMX_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************//**
 * version
 ******************************************************************************/

/**
 * @brief Library version can be obtained using command:
 *  $ strings libmemx.so | grep "libmemx version".
 * Library version 'x.y.z' represents and should be modified in case:
 *  x: new memryx device support,
 *  y: top level memx api changed,
 *  z: minor bug fix.
 */
#define MEMX_LIBRARY_VERSION "2.12.1"

/***************************************************************************//**
 * common
 ******************************************************************************/
#ifndef MEMX_COMMON_H_
#define MEMX_COMMON_H_
#if defined(__MSC_VER) || defined(WIN_EXPORTS)
  #define MEMX_API_EXPORT __declspec(dllexport)
  #define MEMX_API_IMPORT __declspec(dllimport)
#elif defined(__GNUC__)
  #define MEMX_API_EXPORT __attribute__((visibility("default")))
  #define MEMX_API_IMPORT
#else
  #define MEMX_API_EXPORT
  #define MEMX_API_IMPORT
#endif
#ifndef unused
#define unused(x) (void)(x)
#endif
#endif /* MEMX_COMMON_H_ */

/***************************************************************************//**
 * status
 ******************************************************************************/
#ifndef MEMX_STATUS_H_
#define MEMX_STATUS_H_
/**
 * @brief Helper macro to check if there is no error occurs.
 */
#define memx_status_no_error(_status_) ((_status_) == MEMX_STATUS_OK)

/**
 * @brief Helper macro to check if there is any error occurs.
 */
#define memx_status_error(_status_) ((_status_) != MEMX_STATUS_OK)

/**
 * @brief Driver internal error code. '0' should always be used to indicate no
 * error occurs.
 */
typedef enum _memx_status {
  MEMX_STATUS_OK = 0,
  MEMX_STATUS_OTHERS = 1,
} memx_status;

#endif /* MEMX_STATUS_H_ */

typedef enum _memx_get_feature_opcode {
  OPCODE_GET_MANUFACTURERID          = 0,
  OPCODE_GET_FW_COMMIT               = 1,
  OPCODE_GET_DATE_CODE               = 2,
  OPCODE_GET_COLD_WARM_REBOOT_COUNT  = 3,
  OPCODE_GET_WARM_REBOOT_COUNT       = 4,
  OPCODE_GET_KDRIVER_VERSION         = 5,
  OPCODE_GET_TEMPERATURE             = 6,
  OPCODE_GET_THERMAL_STATE           = 7,
  OPCODE_GET_THERMAL_THRESHOLD       = 8,
  OPCODE_GET_FREQUENCY               = 9,
  OPCODE_GET_VOLTAGE                 = 10,
  OPCODE_GET_THROUGHPUT              = 11,
  OPCODE_GET_POWER                   = 12,
  OPCODE_GET_POWERMANAGEMENT         = 13,
  OPCODE_GET_POWER_ALERT             = 14,
  OPCODE_GET_MODULE_INFORMATION      = 15,
  OPCODE_GET_INTERFACE_INFO          = 16,
  OPCODE_GET_IFMAP_CONTROL           = 17,
  OPCODE_GET_HW_INFO                 = 18,
  OPCODE_GET_QSPI_RESET_RELEASE      = 19,
  OPCODE_GET_MPU_UTILIZATION         = 20,
  OPCODE_GET_FREQUENCY_EFFECTIVE     = 21,
  OPCDOE_GET_DEVICE_DMA_TRIGGER_TYPE = 22,
  OPCODE_GET_FEATURE_MAX
} memx_get_feature_opcode;

typedef enum _memx_set_feature_opcode {
  OPCODE_SET_THERMAL_THRESHOLD       = 0,
  OPCODE_SET_FREQUENCY               = 1,
  OPCODE_SET_VOLTAGE                 = 2,
  OPCODE_SET_POWERMANAGEMENT         = 3,
  OPCODE_SET_POWER_THRESHOLD         = 4,
  OPCODE_SET_POWER_ALERT_FREQUENCY   = 5,
  OPCODE_SET_IFMAP_CONTROL           = 6,
  OPCODE_SET_QSPI_RESET_RELEASE      = 7,
  OPCDOE_SET_DEVICE_DMA_TRIGGER_TYPE = 8,
  OPCODE_SET_FEATURE_MAX
} memx_set_feature_opcode;

typedef enum _memx_selftest_opcode {
  OPCODE_SELFTEST_RESERVED          = 0,
  OPCODE_SELFTEST_PCIE_BANDWIDTH    = 1,
  OPCODE_SELFTEST_MAX
} memx_selftest_opcode;

typedef enum _memx_devioctrl_opcode {
  OPCODE_DEVIOCTRL_I2C_RW              = 0,
  OPCODE_DEVIOCTRL_GPIO_R              = 1,
  OPCODE_DEVIOCTRL_GPIO_W              = 2,
  OPCODE_DEVIOCTRL_MAX
} memx_devioctrl_opcode;

typedef enum {
	MEMX_PS0, 	//Operational power state (I/O Support)
	MEMX_PS1,		//Operational power state (I/O Support)
	MEMX_PS2, 	//Non-operational power state (I/O not Support)
	MEMX_PS3, 	//Non-operational power state (I/O not Support)
  MEMX_PS4, 	//Non-operational power state (I/O not Support, xFlow not Support, device based)
	MAX_POWER_STATE
} MEMX_POWER_STATE;

typedef enum {
  MXMX_BOOT_MODE_QSPI = 0,
  MXMX_BOOT_MODE_USB = 1,
  MXMX_BOOT_MODE_PCIE = 2,
  MXMX_BOOT_MODE_UART = 3,
  MXMX_BOOT_MODE_MAX
} MXMX_BOOT_MODE;

typedef enum {
  MXMX_CHIP_VERSION_A0 = 0,
  MXMX_CHIP_VERSION_A1 = 5,
  MXMX_CHIP_VERSION_MAX
} MXMX_CHIP_VERSION;

typedef enum {
  MEMX_CHIP_INPUT_DMA_TRIGGER_TYPE_CHIP = 0x0,
  MEMX_CHIP_INPUT_DMA_TRIGGER_TYPE_HOST = 0x1,
  MEMX_CHIP_INPUT_DMA_TRIGGER_TYPE_MAX
} MEMX_CHIP_INPUT_DMA_TRIGGER_TYPE_t;

#define GET_BOOT_MODE(module_info) ((module_info >> 32) & 0x3)
#define GET_CHIP_VERSION(module_info) (module_info & 0xF)

typedef struct memx_throughput_information {
	unsigned int igr_from_host_us;
	unsigned int igr_from_host_kb;
	unsigned int igr_to_mpu_us;
	unsigned int igr_to_mpu_kb;
	unsigned int egr_from_mpu_us;
	unsigned int egr_from_mpu_kb;
	unsigned int egr_to_host_us;
	unsigned int egr_to_host_kb;
	unsigned int kdrv_tx_us;
	unsigned int kdrv_tx_kb;
	unsigned int kdrv_rx_us;
	unsigned int kdrv_rx_kb;
	unsigned int udrv_write_us;
	unsigned int udrv_write_kb;
	unsigned int udrv_read_us;
	unsigned int udrv_read_kb;
} memx_throughput_information;

typedef struct memx_fmap_buf_t {
  size_t size;
  size_t idx;
  uint8_t *data;
} memx_fmap_buf_t;

/******************************************************************************/

/**
 * @brief MemryX device MX3(pre-prod):Cascade. Constant value which should be referenced
 * only and not be modified manually.
 */
#define MEMX_DEVICE_CASCADE (30)

/**
 * @brief MemryX device MX3:Cascade+. Constant value which should be referenced
 * only and not be modified manually.
 */
#define MEMX_DEVICE_CASCADE_PLUS (31)

/**
 * @brief Maximum number of model contexts can be stored within driver. Constant
 * value which should be referenced only and not be modified manually.
 */
#define MEMX_MODEL_MAX_NUMBER (32)

/**
 * @brief Maximum number of MPU device group contexts can be stored within
 * driver. Constant value which should be referenced only and not be modified
 * manually.
 */
#define MEMX_DEVICE_GROUP_MAX_NUMBER (128)

/**
 * @brief Option to configure model input or output format to 32-bits floating point.
 * By default, input and output feature map should be configured using this option
 * if model input and output are using floating-point.
 */
#define MEMX_FMAP_FORMAT_FLOAT32 (5)

/**
 * @brief Option to configure model input or output format to bfloat16.
 * By default, input and output feature map should be configured using this option
 * if model input and output are using floating-point.
 */
#define MEMX_FMAP_FORMAT_BF16 (4)

/**
 * @brief Option to configure model input or output format to raw byte array.
 * By default, input and output feature map should be configured using this option
 * if model input and output are using floating-point.
 */
#define MEMX_FMAP_FORMAT_RAW (2)

/**
 * @brief Option to configure model input or output feature map format to
 * MemryX proprietary format group-bfloat-80.
 */
#define MEMX_FMAP_FORMAT_GBF80 (0)
#define MEMX_FMAP_FORMAT_GBF80_ROW_PAD (6)

/**
 * @brief Option of `memx_download_model()` to hint the data source type.
 *        All Data source type could not be used togeter (exclusive)
 */
#define MEMX_DOWNLOAD_TYPE_FROM_CACHE               (1 << 5)
#define MEMX_DOWNLOAD_TYPE_FROM_FILE                (0 << 7)
#define MEMX_DOWNLOAD_TYPE_FROM_BUFFER              (1 << 7)

/**
 * @brief Option of `memx_download_model()` to hint using legacy way to download Weight Memory
 */
#define MEMX_DOWNLOAD_TYPE_WTMEM_LEGACY             (1 << 6)

/**
 * @brief Option of `memx_download_model()` to download weight memory only to
 * device. Can be used together with `MEMX_DOWNLOAD_TYPE_MODEL`.
 */
#define MEMX_DOWNLOAD_TYPE_WTMEM (1 << 0)

/**
 * @brief Option of `memx_download_model()` to download model only to device.
 * Can be used together with `MEMX_DOWNLOAD_TYPE_WTMEM`.
 */
#define MEMX_DOWNLOAD_TYPE_MODEL (1 << 1)

/**
 * @brief Option of `memx_download_model()` to download both weight memory and
 * model to device. The same effect as using `MEMX_DOWNLOAD_TYPE_WTMEM` and
 * `MEMX_DOWNLOAD_TYPE_MODEL` together.
 */
#define MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL (MEMX_DOWNLOAD_TYPE_WTMEM | MEMX_DOWNLOAD_TYPE_MODEL)

/**
 * @brief Option of `memx_download_model()`.The same effect as using
 * `MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL`, but using Legacy way to download WTMEM.
 */
#define MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL_LEGACY (MEMX_DOWNLOAD_TYPE_WTMEM_LEGACY | MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL)


/**
 * @brief Option of `memx_download_model()`.The same effect as using
 * `MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL`, but using buffer pointer.
 */
#define MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL_BUFFER (MEMX_DOWNLOAD_TYPE_FROM_BUFFER | MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL)

/**
 * @brief Option of `memx_download_model()`.The same effect as using
 * `MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL`, but using cache entry from upper layer.
 */
#define MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL_CACHE (MEMX_DOWNLOAD_TYPE_FROM_CACHE | MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL)


/**
 * @brief Option of `memx_download_model()`.The same effect as using
 * `MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL`, but using buffer pointer and Legacy way to download WTMEM.
 */
#define MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL_BUFFER_LEGACY (MEMX_DOWNLOAD_TYPE_FROM_BUFFER | MEMX_DOWNLOAD_TYPE_WTMEM_LEGACY | MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL)


/**
 * @brief Option of `memx_config_mpu_group()` to set different MPU group
 */
#define MEMX_MPU_GROUP_CONFIG_ONE_GROUP_FOUR_MPUS    (0)
#define MEMX_MPU_GROUP_CONFIG_TWO_GROUP_TWO_MPUS     (1)
#define MEMX_MPU_GROUP_CONFIG_ONE_GROUP_ONE_MPU      (2)
#define MEMX_MPU_GROUP_CONFIG_ONE_GROUP_THREE_MPUS   (3)
#define MEMX_MPU_GROUP_CONFIG_ONE_GROUP_TWO_MPUS     (4)
#define MEMX_MPU_GROUP_CONFIG_ONE_GROUP_EIGHT_MPUS   (5)
#define MEMX_MPU_GROUP_CONFIG_ONE_GROUP_TWELVE_MPUS  (6)
#define MEMX_MPU_GROUP_CONFIG_ONE_GROUP_SIXTEEN_MPUS (7)

/***************************************************************************//**
 * command
 ******************************************************************************/
/**
 * @brief All MPU and MPUIO command should be registered here to obtain a
 * global unique ID. New command added should be appended to tail. The reason
 * why MPU and MPUIO share the same enumeration is because that command through
 * MPU interface will possibly be forwarded to MPUIO once not recognized.
 */
typedef enum _memx_command {
  MEMX_CMD_READ_TOTAL_CHIP_COUNT = 0,
  MEMX_CMD_GET_FW_DOWNLOAD_STATUS = 1,
  MEMX_CMD_CONFIG_MPU_GROUP = 2,
  MEMX_CMD_RESET_DEVICE = 3,
  MEMX_CMD_SET_QSPI_RESET_RELEASE = 4,
  MEMX_CMD_GET_QSPI_RESET_RELEASE = 5,
  MEMX_CMD_MAX,
} memx_command;

/**
 * @brief Acquires mutex lock from driver. To be noticed, this function will
 * not actually lock any operation but only acquire the unique mutex within
 * driver. Lock is only required in multi-threading or multi-processing case to
 * avoid driver access conflict when multiple processes trying to re-configure
 * the same hardware device. Also, this function will block until mutex is
 * acquired successfully. See `memx_trylock()` for non-blocking mutex lock.
 *
 * ~~~~~
 * // lock MPU device group 0
 * memx_lock(0);
 * printf("lock success.\n");
 * // always remember to release lock finally
 * memx_unlock(0);
 * ~~~~~
 *
 * @see Advanced Driver Usage: Example 3: Runtime Model Swap
 *
 * @param group_id            MPU device group ID
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_lock(uint8_t group_id);

/**
 * @brief Acquires mutex lock from driver. Apart from `memx_lock()`, this
 * function returns immediately if fail to acquire lock.
 *
 * ~~~~~
 * // do something only if lock MPU device group 0 successfully
 * if (memx_trylock(0) == 0) {
 *   printf("lock success.\n");
 *   // always remember to release lock finally
 *   memx_unlock(0);
 * } else {
 *   printf("lock failure.\n");
 * }
 * ~~~~~
 *
 * @param group_id            MPU device group ID
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_trylock(uint8_t group_id);

/**
 * @brief Releases the mutex lock acquired from driver. Be careful and not to
 * unexpectedly release mutex which is not locked by current process. Uses
 * `memx_lock()` and `memx_trylock()` to acquire lock first.
 *
 * @param group_id            MPU device group ID
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_unlock(uint8_t group_id);

/**
 * @brief Open new model context with specified MPU device driver and MPU device
 * group binding to it. Internal reference count increases if the same model
 * ID is re-used and opened multiple times. All operations later using this
 * model ID will use binded MPU device driver and MPU device group.
 * Currently, at most 32 model contexts (model ID = 0 ~ 31) can be stored
 * within driver.
 *
 * ~~~~~
 * // bind MPU device group 0 as MX3 to model 0
 * memx_status status = memx_open(0, 0, MEMX_DEVICE_CASCADE_PLUS);
 * if (status == 0) {
 *   printf("open success.\n");
 * } else {
 *   printf("open failure.\n");
 * }
 * // always remember to close device finally
 * memx_close(0);
 * ~~~~~
 *
 * @see Classification (Driver APIs): Basic Inference
 *
 * @param model_id            model ID
 * @param group_id            MPU device group ID
 * @param chip_gen            Deprecated, user don't care. device chip_gen was detected by driver
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_open(uint8_t model_id, uint8_t group_id, float chip_gen);

/**
 * @brief Close current connected MPU device group and destory model context
 * stored in driver. If model context is opened multiple times, calling close
 * will decrease internal reference count one each time. Only after reference
 * count goes down to zero will model context finally be released. So make sure
 * to use equivalent times of open and close to clean-up driver resources.
 * Otherwise, it is safe to call close model ID which is not opened or is closed
 * already. See `memx_open()` for more information.
 *
 * @param model_id            model ID
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_close(uint8_t model_id);

/**
 * @brief General purpose operation. Reserved for future.
 *
 * @param model_id            model ID
 * @param cmd_id              command ID
 * @param data                command data
 * @param size                command data size
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_operation(uint8_t model_id, uint32_t cmd_id, void* data, uint32_t size);

/**
 * @brief Download DFP (Data Flow Program) weight memory to device. DFP file is
 * generated by MIX and should by default be named with extension `.dfp`. This
 * function download only weight memory with no model setting to device. Use
 * `memx_download_model()` with option instead to download both weight memory
 * and model to device.
 *
 * @param model_id            model ID
 * @param file_path           DFP file path, only support from file
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_download_model_wtmem(uint8_t model_id, const char* file_path);

/**
 * @brief Download DFP (Data Flow Program) model to device. DFP file is
 * generated by MIX and should by default be named with extension `.dfp`. This
 * function download only model setting with no weight memory to device. Use
 * `memx_download_model()` with option instead to download both weight memory
 * and model to device.
 *
 * @param model_id            model ID
 * @param file_path           DFP file path, only support from file
 * @param model_idx           model index within DFP file, give '0' if there is only one model compiled in DFP file
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_download_model_config(uint8_t model_id, const char* file_path, uint8_t model_idx);

/**
 * @brief Download weight memory and model to device based on given download
 * type selection. If both weight memory and model download are required, weight
 * memory download will be performed before model download. After model download
 * is completed,  input and output feature map shape will be configured to
 * driver to allocate resources automatically. After model download and before
 * data can be transferred to device, `memx_set_stream_enable()` must be called
 * to change driver internal state from idle to data-transfer.
 *
 * ~~~~~
 * // bind MPU device group 0 as MX3 to model 0
 * memx_status status = memx_open(0, 0, MEMX_DEVICE_CASCADE_PLUS);
 * if (status == 0) {
 *   // download weight memory and 1st model within DFP file to device
 *   status = memx_download_model(0, "model.dfp", 0,
 *     MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL);
 * }
 * // always remember to close device finally
 * memx_close(0);
 * ~~~~~
 *
 * ~~~~~
 * // download weight memory and 1st model within DFP file to device separately
 * // same effect as example using type MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL
 *
 * // bind MPU device group 0 as MX3 to model 0
 * memx_status status = memx_open(0, 0, MEMX_DEVICE_CASCADE_PLUS);
 * if (status == 0) {
 *   // download weight memory to device, model index within DFP will be ignored
 *   status = memx_download_model(0, "model.dfp", 0, MEMX_DOWNLOAD_TYPE_WTMEM);
 * }
 * if (status == 0) {
 *   // download 1st model within DFP file to device
 *   status = memx_download_model(0, "model.dfp", 0, MEMX_DOWNLOAD_TYPE_MODEL);
 * }
 * // always remember to close device finally
 * memx_close(0);
 * ~~~~~
 *
 * @param model_id            model ID
 * @param file_path           DFP file path
 * @param model_idx           model index within DFP file, give '0' if there is only one model compiled in DFP file.
 *                            model index will be ignored if download type selection is weight memory only
 * @param type                0: ignored, 1: weight memory only, 2: model only, 3: both weight memory and model
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_download_model(uint8_t model_id, const char* file_path, uint8_t model_idx, int32_t type);

/**
 * @brief Download firmware to device flash.
 *
 *
 * @param model_id            device group ID
 * @param data                firmware file path or buffer
 * @param type                0: Download from file, MEMX_DOWNLOAD_TYPE_FROM_BUFFER: From Buffer
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_download_firmware(uint8_t group_id, const char *data, uint8_t type);

/**
 * @brief Enable all input and output data to be transferred to device of
 * specified model. When model context is first initialized, all data transfers
 * are blocked (but model download is allowed) to avoid data being transferred
 * to device before model download is finished.
 *
 * ~~~~~
 * // bind MPU device group 0 as MX3 to model 0
 * memx_status status = memx_open(0, 0, MEMX_DEVICE_CASCADE_PLUS);
 * if (status == 0) {
 *   // download weight memory and 1st model within DFP file to device
 *   status = memx_download_model(0, "model.dfp", 0,
 *     MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL);
 * }
 * if (status == 0) {
 *   // enable all data being transferred to device
 *   status = memx_set_stream_enable(0, 0);
 * }
 * // always remember to close device finally
 * memx_close(0);
 * ~~~~~
 *
 * @param model_id            model ID
 * @param wait                wait until driver state change from idle to data-transfer complete
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_set_stream_enable(uint8_t model_id, int32_t wait);

/**
 * @brief Disable all input and output data to be transferred to device of
 * specified model. This function is used to guarantee no data from specified
 * model will be transferred to device in case multiple models are sharing the
 * the same hardware resource and want to transfer data in the same time. Use
 * `memx_set_stream_enable()` to resume data transfer of specified model.
 *
 * @param model_id            model ID
 * @param wait                wait until driver state change from data-transfer to idle complete
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_set_stream_disable(uint8_t model_id, int32_t wait);

/**
 * @brief Configure driver input feature map internal queue size. No more input
 * feature map will be allowed to write to driver if driver internal queue full.
 * An error will be reported in case driver internal queue full and data is not
 * copied to driver. All input flows share the same queue size configuration
 * with a default queue size to '4'.
 *
 * @param model_id            model ID
 * @param size                input feature map queue size
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_set_ifmap_queue_size(uint8_t model_id, int32_t size);

/**
 * @brief Configure driver output feature map internal queue size. No more
 * output feature map will be allowed to read from device to driver if driver
 * internal queue full. In case driver internal queue full which causes driver
 * does not asking data transfer from device, data flow might be blocked and
 * back pressure to input feature map causing no more input feature map will be
 * allowed to write to device. All output flows share the same queue size
 * configuration with a default queue size to '4'.
 *
 * @param model_id            model ID
 * @param size                output feature map queue size
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_set_ofmap_queue_size(uint8_t model_id, int32_t size);

/**
 * @brief Read back input feature map shape configured of specific flow (port).
 * Returns failure with variables set to all zeros in case given flow is not
 * configured.
 *
 * ~~~~~
 * // bind MPU device group 0 as MX3 to model 0
 * memx_status status = memx_open(0, 0, MEMX_DEVICE_CASCADE_PLUS);
 * if (status == 0) {
 *   // download weight memory and 1st model within DFP file to device
 *   status = memx_download_model(0, "model.dfp", 0,
 *     MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL);
 * }
 * if (status == 0) {
 *   // read back flow 0 (port 0) input feature map shape for debug information
 *   int32_t height, width, z, channel_number, format;
 *   memx_get_ifmap_size(0, 0, &height, &width, &z, &channel_number, &format);
 *   // for example: shape = (224, 224, 1, 3), format = 1
 *   printf("shape = (%d, %d, %d, %d), format = %d\n",
 *     height, width, z, channel_number, format);
 * }
 * // always remember to close device finally
 * memx_close(0);
 * ~~~~~
 *
 * @param model_id            model ID
 * @param flow_id             input flow (port) ID
 * @param height              input feature map height
 * @param width               input feature map width
 * @param z                   input feature map z
 * @param channel_number      input feature map channel number
 * @param format              input feature map format
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_get_ifmap_size(uint8_t model_id, uint8_t flow_id, int32_t* height, int32_t* width, int32_t* z, int32_t* channel_number, int32_t* format);

/**
 * @brief Read back input feature map range conversion configured of specific
 * flow (port). Returns failure with variables set to all zeros in case flow is
 * not configured.
 *
 * @param model_id            model ID
 * @param flow_id             input flow (port) ID
 * @param enable              input feature map data range conversion is enabled
 * @param shift               amount to shift before scale
 * @param scale               amount to scale before integer cast
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_get_ifmap_range_convert(uint8_t model_id, uint8_t flow_id, int32_t* enable, float* shift, float* scale);

/**
 * @brief Read back output feature map shape configured of specific flow (port).
 * Returns failure with variables set to all zeros in case given flow is not
 * configured.
 *
 * ~~~~~
 * // bind MPU device group 0 as MX3 to model 0
 * memx_status status = memx_open(0, 0, MEMX_DEVICE_CASCADE_PLUS);
 * if (status == 0) {
 *   // download weight memory and 1st model within DFP file to device
 *   status = memx_download_model(0, "model.dfp", 0,
 *     MEMX_DOWNLOAD_TYPE_WTMEM_AND_MODEL);
 * }
 * if (status == 0) {
 *   // read back flow 0 (port 0) input feature map shape for debug information
 *   int32_t height, width, z, channel_number, format;
 *   memx_get_ofmap_size(0, 0, &height, &width, &z, &channel_number, &format);
 *   // for example: shape = (224, 224, 1, 3), format = 1
 *   printf("shape = (%d, %d, %d, %d), format = %d\n",
 *     height, width, z, channel_number, format);
 * }
 * // always remember to close device finally
 * memx_close(0);
 * ~~~~~
 *
 * @param model_id            model ID
 * @param flow_id             output flow (port) ID
 * @param height              output feature map height
 * @param width               output feature map width
 * @param z                   output feature map z
 * @param channel_number      output feature map channel number
 * @param format              output feature map format
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_get_ofmap_size(uint8_t model_id, uint8_t flow_id, int32_t* height, int32_t* width, int32_t* z, int32_t* channel_number, int32_t* format);

/**
 * @brief Read back output feature map HPOC (High-Precision-Output-Channel)
 * setting. The 'hpoc_indexes' returned is owned by driver in case 'hpoc_size'
 * larger than zero, and should not be modified by user.
 *
 * @param model_id            model ID
 * @param flow_id             output flow (port) ID
 * @param hpoc_size           high-precision-output-channel number
 * @param hpoc_indexes        high-precision-output-channel index array (read-only, do not modify)
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_get_ofmap_hpoc(uint8_t model_id, uint8_t flow_id, int32_t* hpoc_size, int32_t** hpoc_indexes);

/**
 * @brief Get device count
 *
 * @param pData    Buffer from host to get device count
 *
 * @return memx_status Return status from driver
 */
MEMX_API_EXPORT memx_status memx_operation_get_device_count(void *pData);

/**
 * @brief Get group count by model_id
 *
 * @param model_id Model index
 * @param pData    Buffer from host to get group count
 *
 * @return memx_status Return status from driver
 */
MEMX_API_EXPORT memx_status memx_operation_get_mpu_group_count(uint8_t group_id, void *pData);

/**
 * @brief Data channel write input feature map of specified flow (port) to
 * device. This function must be called after `memx_download_model()`.
 *
 * ~~~~~
 * memx_status status = MEMX_STATUS_OK;
 * if (status == 0) {
 *   // write data to model 0 flow 0 (port 0) to run inference
 *   status = memx_stream_ifmap(0, 0, ifmap, 200);
 * }
 * ~~~~~
 *
 * @param model_id            model ID
 * @param flow_id             input flow (port) ID
 * @param ifmap               input feature map (frame)
 * @param timeout             milliseconds timeout, '0' indicates infinite
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_stream_ifmap(uint8_t model_id, uint8_t flow_id, void* ifmap, int32_t timeout);

/**
 * @brief Data channel read output feature map of specified flow (port) from
 * device. This function must be called after `memx_download_model()`.
 *
 * ~~~~~
 * memx_status status = MEMX_STATUS_OK;
 * if (status == 0) {
 *   // read data from model 0 flow 0 (port 0) as inference result
 *   status = memx_stream_ofmap(0, 0, ofmap, 200);
 * }
 * ~~~~~
 *
 * @param model_id            model ID
 * @param flow_id             output flow (port) ID
 * @param ofmap               output feature map (frame)
 * @param timeout             milliseconds timeout, '0' indicates infinite
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_stream_ofmap(uint8_t model_id, uint8_t flow_id, void* ofmap, int32_t timeout);

/**
 * @brief Config MPU group on the device group, support MEMX_MPU_GROUP_CONFIG_*
 *
 * @param group_id            device group ID
 * @param mpu_group_config    MPU group config
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_config_mpu_group(uint8_t group_id, uint8_t mpu_group_config);

/**
 * @brief Read back the chip gen for the currently active model.
 *
 * @param model_id            model ID
 * @param chip_gen            chip gen of device (MEMX_MPU_CHIP_GEN_CASCADE, MEMX_MPU_CHIP_GEN_CASCADE_PLUS)
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_get_chip_gen(uint8_t model_id, uint8_t* chip_gen);

/**
 * @brief Direct Change power state for specific model
 *
 * @param model_id            model ID
 * @param state               This field indicates the new power state into which the controller is requested to transition.
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_set_powerstate(uint8_t model_id, uint8_t state);

/**
 * @brief Force Device Enter Deep Sleep Mode
 *
 * @param group_id            group ID
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_enter_device_deep_sleep(uint8_t group_id);

/**
 * @brief Force Device Exit Deep Sleep Mode
 *
 * @param group_id            group ID
 *
 * @return 0 on success, otherwise error code
 * @note   Must be call before accesing when device is in deep sleep state
 */
MEMX_API_EXPORT memx_status memx_exit_device_deep_sleep(uint8_t group_id);

/**
 * @brief Read back the chip count for the currently devices.
 *
 * @param group_id            Group(device) id.
 * @param chip_count          Pointer for chip count result.
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_get_total_chip_count(uint8_t group_id, uint8_t* chip_count);

/**
 * @brief Device get-feature for specific device and chip.
 *
 * @param group_id Group(device) id.
 * @param chip_id  Chip id.
 * @param opcode   Operation code for get-feature
 * @param buffer   User buffer pointer for get-feature.
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_get_feature(uint8_t group_id, uint8_t chip_id, memx_get_feature_opcode opcode, void* buffer);

/**
 * @brief Device set-feature for specific device.
 *
 * @param group_id  Group(device) id.
 * @param opcode    Operation code for get-feature.
 * @param parameter Parameter for specific operation code.
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_set_feature(uint8_t group_id, uint8_t chip_id, memx_set_feature_opcode opcode, uint16_t parameter);

/**
 * @brief Enqueue input feature map buffer of specified flow (port) into device.
 * This function must be called after `memx_download_model()`.
 *
 * @param model_id            model ID
 * @param flow_id             input flow (port) ID
 * @param fmap_buf            feature map buffer
 * @param timeout             milliseconds timeout, '0' indicates infinite
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_enqueue_ifmap_buf(uint8_t model_id, uint8_t flow_id, memx_fmap_buf_t* fmap_buf, int32_t timeout);

/**
 * @brief Enqueue output feature map buffer of specified flow (port) into device.
 * This function must be called after `memx_download_model()`.
 *
 * @param model_id            model ID
 * @param flow_id             output flow (port) ID
 * @param fmap_buf            feature map buffer
 * @param timeout             milliseconds timeout, '0' indicates infinite
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_enqueue_ofmap_buf(uint8_t model_id, uint8_t flow_id, memx_fmap_buf_t* fmap_buf, int32_t timeout);

/**
 * @brief Dequeue input feature map buffer of specified flow (port) from device.
 * This function must be called after `memx_download_model()`.
 *
 * @param model_id            model ID
 * @param flow_id             input flow (port) ID
 * @param fmap_buf            feature map buffer
 * @param timeout             milliseconds timeout, '0' indicates infinite
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_dequeue_ifmap_buf(uint8_t model_id, uint8_t flow_id, memx_fmap_buf_t* fmap_buf, int32_t timeout);

/**
 * @brief Dequeue output feature map buffer of specified flow (port) from device.
 * This function must be called after `memx_download_model()`.
 *
 * @param model_id            model ID
 * @param flow_id             output flow (port) ID
 * @param fmap_buf            feature map buffer
 * @param timeout             milliseconds timeout, '0' indicates infinite
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_dequeue_ofmap_buf(uint8_t model_id, uint8_t flow_id, memx_fmap_buf_t* fmap_buf, int32_t timeout);

/**
 * @brief Abort command for current connected MPU device group.
 * This function must be used in 'unexpected termination situations' (e.g. ctrl-c event).
 * memx_close must be called after this function to make sure dummy read will be triggered.
 *
 * @param model_id            model ID
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_set_abort_read(uint8_t model_id);

/**
 * @brief Device self-test for specific device and chip.
 *
 * @param group_id Group(device) id.
 * @param chip_id  Chip id.
 * @param opcode   Operation code for self-test
 * @param buffer   User buffer pointer for self-test.
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_self_test(uint8_t group_id, uint8_t chip_id, memx_selftest_opcode opcode, void* buffer);

/**
 * @brief Device IO control for specific device and chip.
 *
 * @param group_id Group(device) id.
 * @param chip_id  Chip id.
 * @param opcode   Operation code for device io
 * @param buffer   User buffer pointer for device io paramter.
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_devio_control(uint8_t group_id, uint8_t chip_id, memx_devioctrl_opcode opcode, void* buffer);

/**
 * @brief Download weight memory and model to device based on given DFP Cache Entry
 * After model download is completed,  input and output feature map shape will be configured to
 * driver to allocate resources automatically. After model download and before
 * data can be transferred to device, `memx_set_stream_enable()` must be called
 * to change driver internal state from idle to data-transfer.
 *
 * @param model_id            model ID
 * @param pContext            DFP cahce entry pointer from upper layer
 * @param model_idx           model index within DFP file only support 0
 * @param type                0: ignored, 1: weight memory only, 2: model only, 3: both weight memory and model
 *
 * @return 0 on success, otherwise error code
 */
MEMX_API_EXPORT memx_status memx_download_model_from_cahce(uint8_t model_id, void *pContext, uint8_t model_idx, int type);
#ifdef __cplusplus
}
#endif

#endif /* MEMX_H_ */
