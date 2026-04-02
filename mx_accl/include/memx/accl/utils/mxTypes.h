// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef MX_TYPES_H
#define MX_TYPES_H

#pragma once
#include <vector>
#include <stdint.h>
#include <stdexcept>
#include <iostream>
#include <string>
#include <functional>

// for MEMX_API_EXPORT macro
#include <memx/memx.h>

namespace MX
{
namespace Types
{

// version info string
constexpr const char* RUNTIME_VERSION = "2.2.0";


class FeatureMap; // forward declaration

// type definition for user callbacks
typedef std::function<bool(std::vector<const FeatureMap*>, int stream_id)> callback_t;

/**
 * @class ShapeVector
 * @brief Represents the shape of a tensor with flexible dimension ordering.
 *
 * The ShapeVector class encapsulates tensor shape information using a fixed-size or dynamic vector.
 * It provides convenience methods for interpreting and converting between channel-first and
 * channel-last formats. Internally, the shape is represented using four components by default:
 * height (h), width (w), batch (z), and channel (c), but custom-sized shapes are also supported.
 */
class ShapeVector
{
  private:
    std::vector<int64_t> shape; ///< Underlying storage for shape dimensions.
    int64_t h = 0, w = 0, z = 0, c = 0; ///< Canonical height, width, batch, and channel dimensions.
    int size_ = 4; ///< Default shape size.

  public:
    /**
     * @brief Default constructor. Initializes the shape with 4 dimensions, all set to 0.
     */
    MEMX_API_EXPORT ShapeVector();

    /**
     * @brief Construct a ShapeVector using explicit dimensions.
     * @param h Height.
     * @param w Width.
     * @param z Batch size.
     * @param c Channel count.
     */
    MEMX_API_EXPORT ShapeVector(int64_t h, int64_t w, int64_t z, int64_t c);

    /**
     * @brief Construct a ShapeVector of custom size with all dimensions initialized to 1.
     * @param size Number of dimensions.
     */
    MEMX_API_EXPORT ShapeVector(int size);

    /**
     * @brief Accessor for a specific dimension by index (non-const).
     * @param index Dimension index.
     * @return Reference to the dimension value at the given index.
     */
    MEMX_API_EXPORT int64_t &operator[](int64_t index);

    /**
     * @brief Returns the shape in channel-first format (e.g., [C, H, W, Z]).
     * @return A vector of dimensions in channel-first order.
     */
    MEMX_API_EXPORT std::vector<int64_t> chfirst_shape();

    /**
     * @brief Returns the shape in channel-last format (e.g., [H, W, Z, C]).
     * @return A vector of dimensions in channel-last order.
     */
    MEMX_API_EXPORT std::vector<int64_t> chlast_shape();

    /**
     * @brief Returns a pointer to the raw shape data.
     * @return Pointer to the first element of the shape vector.
     */
    MEMX_API_EXPORT int64_t* data();

    /**
     * @brief Returns the number of dimensions in the shape.
     * @return Number of dimensions.
     */
    MEMX_API_EXPORT int64_t size() const;

    /**
     * @brief Sets the internal shape to follow channel-first layout.
     */
    MEMX_API_EXPORT void set_ch_first();

    /**
     * @brief equality operator to compare two ShapeVector objects
     */
    MEMX_API_EXPORT bool operator==(const ShapeVector& other) const;
    MEMX_API_EXPORT bool operator!=(const ShapeVector& other) const;

    /**
     * @brief Returns a string representation of the shape for debugging purposes.
     */
    MEMX_API_EXPORT std::string to_string() const;
};

/**
 * @struct MxModelInfo
 * @brief Holds metadata and configuration details for a compiled model.
 *
 * The MxModelInfo struct contains essential information about a model compiled
 * for execution, including the number of input and output feature maps, their
 * shapes and sizes, and the associated layer names. This metadata is typically
 * used during runtime setup, validation, or for constructing input/output buffers.
 *
 * @var MxModelInfo::model_index
 * Unique index identifying the model instance.

 * @var MxModelInfo::num_in_featuremaps
 * Number of input feature maps required by the model.

 * @var MxModelInfo::num_out_featuremaps
 * Number of output feature maps produced by the model.

 * @var MxModelInfo::input_layer_names
 * Names of the model's input layers, listed in the order expected by the runtime.

 * @var MxModelInfo::output_layer_names
 * Names of the model's output layers, listed in the order produced by the model.

 * @var MxModelInfo::in_featuremap_shapes
 * Shapes of the input feature maps, represented as a vector of ShapeVector objects.

 * @var MxModelInfo::out_featuremap_shapes
 * Shapes of the output feature maps, represented as a vector of ShapeVector objects.

 * @var MxModelInfo::in_featuremap_sizes
 * Sizes (in bytes or elements, depending on context) of each input feature map.

 * @var MxModelInfo::out_featuremap_sizes
 * Sizes (in bytes or elements, depending on context) of each output feature map.
 */
struct MxModelInfo {
    int model_index;
    int num_in_featuremaps;
    int num_out_featuremaps;
    std::vector<std::string> input_layer_names;
    std::vector<std::string> output_layer_names;
    std::vector<MX::Types::ShapeVector> in_featuremap_shapes;
    std::vector<MX::Types::ShapeVector> out_featuremap_shapes;
    std::vector<std::vector<int64_t>> in_raw_shapes;
    std::vector<std::vector<int64_t>> out_raw_shapes;
    std::vector<size_t> in_featuremap_sizes;
    std::vector<size_t> out_featuremap_sizes;
    bool use_model_shape_in;
    bool use_model_shape_out;
};

/**
 * @enum FrequencyOption
 * @brief Enumeration representing the available frequency options in MHz.
 *
 * Each frequency level corresponds to a specific computational performance in TFLOPS.
 */
enum MxFrequencyOption : uint16_t {
    FREQ_USE_CONF = 0,  ///< Use the current value of /etc/memryx/power.conf instead of overriding
    MX_FREQUENCY_OPTION_INVALID = 1,
    FREQ_200MHz = 200,
    FREQ_225MHz = 225,
    FREQ_250MHz = 250,
    FREQ_275MHz = 275,
    FREQ_300MHz = 300,
    FREQ_325MHz = 325,
    FREQ_350MHz = 350,
    FREQ_375MHz = 375,
    FREQ_400MHz = 400,
    FREQ_425MHz = 425,
    FREQ_450MHz = 450,
    FREQ_475MHz = 475,
    FREQ_500MHz = 500,
    FREQ_525MHz = 525,
    FREQ_550MHz = 550,
    FREQ_575MHz = 575,
    FREQ_600MHz = 600,
    FREQ_625MHz = 625,
    FREQ_650MHz = 650,
    FREQ_675MHz = 675,
    FREQ_700MHz = 700,
    FREQ_725MHz = 725,
    FREQ_750MHz = 750,
    FREQ_775MHz = 775,
    FREQ_800MHz = 800,
    FREQ_825MHz = 825,
    FREQ_850MHz = 850,
    FREQ_875MHz = 875,
    FREQ_900MHz = 900,
    FREQ_925MHz = 925,
    FREQ_950MHz = 950,
    FREQ_975MHz = 975,
    FREQ_1000MHz = 1000
};


inline bool is_valid_frequency_option(uint16_t f)
{
    switch (f) {
        case FREQ_USE_CONF:
        case FREQ_200MHz:
        case FREQ_225MHz:
        case FREQ_250MHz:
        case FREQ_275MHz:
        case FREQ_300MHz:
        case FREQ_325MHz:
        case FREQ_350MHz:
        case FREQ_375MHz:
        case FREQ_400MHz:
        case FREQ_425MHz:
        case FREQ_450MHz:
        case FREQ_475MHz:
        case FREQ_500MHz:
        case FREQ_525MHz:
        case FREQ_550MHz:
        case FREQ_575MHz:
        case FREQ_600MHz:
        case FREQ_625MHz:
        case FREQ_650MHz:
        case FREQ_675MHz:
        case FREQ_700MHz:
        case FREQ_725MHz:
        case FREQ_750MHz:
        case FREQ_775MHz:
        case FREQ_800MHz:
        case FREQ_825MHz:
        case FREQ_850MHz:
        case FREQ_875MHz:
        case FREQ_900MHz:
        case FREQ_925MHz:
        case FREQ_950MHz:
        case FREQ_975MHz:
        case FREQ_1000MHz:
            return true;
        default:
            return false;
    }
}


std::string mxFrequencyOptionToString(MxFrequencyOption freq);


enum MxVoltageOption : uint16_t {
    VOLT_670mV = 670,
    VOLT_675mV = 675,
    VOLT_680mV = 680,
    VOLT_685mV = 685,
    VOLT_690mV = 690,
    VOLT_695mV = 695,
    VOLT_700mV = 700,
    VOLT_705mV = 705,
    VOLT_710mV = 710,
    VOLT_715mV = 715,
    VOLT_720mV = 720,
    VOLT_725mV = 725,
    VOLT_730mV = 730,
    VOLT_735mV = 735,
    VOLT_740mV = 740,
    VOLT_745mV = 745,
    VOLT_750mV = 750,
    VOLT_755mV = 755,
    VOLT_760mV = 760,
    VOLT_765mV = 765,
    VOLT_770mV = 770,
    VOLT_775mV = 775,
    VOLT_780mV = 780,
    VOLT_785mV = 785,
    VOLT_790mV = 790,
    VOLT_795mV = 795,
    VOLT_800mV = 800,
    VOLT_805mV = 805,
    VOLT_810mV = 810,
    VOLT_815mV = 815,
    VOLT_820mV = 820,
    VOLT_825mV = 825,
    VOLT_830mV = 830,
    VOLT_835mV = 835,
    VOLT_840mV = 840,
    VOLT_845mV = 845,
    VOLT_850mV = 850
};

MEMX_API_EXPORT MxVoltageOption getVoltageFromFrequency(MxFrequencyOption freq);

/**
 * @class Pressure
 * @brief Level for approximating how busy the device is. Think of it like "throughput utilization of the running DFP(s)".
 *
 * There are 4 levels of pressure:
 * - LOW: The device is mostly idle. Feel free to assign more work/streams.
 * - MEDIUM: The device is moderately busy. You can assign additional streams, but don't get too crazy.
 * - HIGH: The device is nearing full utilization. Assign more streams with caution.
 * - FULL: The device is fully occupied. Assigning more streams will lower per-stream performance.
 *
 *
 * The levels can be compared against either strings or integers.
 *
 * - LOW is "low" or 0
 * - MEDIUM is "medium" or 1
 * - HIGH is "high" or 2
 * - FULL is "full" or 3
 *
 */
class Pressure
{
  public:
    typedef enum Level : int {
        LOW = 0,
        MEDIUM = 1,
        HIGH = 2,
        FULL = 3
    } Level;
  private:
    Level level;
  public:
    Pressure() : level(Level::LOW) {}
    Pressure(Level lvl) : level(lvl) {}
    Pressure(const std::string &str) : level(fromString(str)) {}
    Pressure(const Pressure &other) : level(other.level) {}
    Pressure(int val) : level(static_cast<Level>(val))
    {
        if (val < 0 || val > 3) {
            throw std::invalid_argument("Invalid pressure level integer: " + std::to_string(val));
        }
    }
    Level fromString(const std::string &str) const
    {
        if (str == "low") { return Level::LOW; }
        if (str == "medium") { return Level::MEDIUM; }
        if (str == "high") { return Level::HIGH; }
        if (str == "full") { return Level::FULL; }
        throw std::invalid_argument("Invalid pressure level string: " + str);
        return Level::FULL;
    }
    std::string toString() const
    {
        switch (level) {
            case Level::LOW: return "low";
            case Level::MEDIUM: return "medium";
            case Level::HIGH: return "high";
            case Level::FULL: return "full";
            default: return "error";
        }
    }
    Pressure &operator=(const Pressure &other)
    {
        if (this != &other) {
            level = other.level;
        }
        return *this;
    }
    Pressure &operator=(const std::string &str)
    {
        level = fromString(str);
        return *this;
    }
    Pressure &operator=(int val)
    {
        if (val < 0 || val > 3) {
            throw std::invalid_argument("Invalid pressure level integer: " + std::to_string(val));
        }
        level = static_cast<Level>(val);
        return *this;
    }
    bool operator==(const Pressure &other) const
    {
        return level == other.level;
    }
    bool operator==(const std::string &str) const
    {
        return level == fromString(str);
    }
    bool operator==(int val) const
    {
        return level == static_cast<Level>(val);
    }
    bool operator!=(const Pressure &other) const
    {
        return level != other.level;
    }
    bool operator!=(const std::string &str) const
    {
        return level != fromString(str);
    }
    bool operator!=(int val) const
    {
        return level != static_cast<Level>(val);
    }
    bool operator<(const Pressure &other) const
    {
        return level < other.level;
    }
    bool operator<(const std::string &str) const
    {
        return level < fromString(str);
    }
    bool operator<(int val) const
    {
        return level < static_cast<Level>(val);
    }
    bool operator<=(const Pressure &other) const
    {
        return level <= other.level;
    }
    bool operator<=(const std::string &str) const
    {
        return level <= fromString(str);
    }
    bool operator<=(int val) const
    {
        return level <= static_cast<Level>(val);
    }
    bool operator>(const Pressure &other) const
    {
        return level > other.level;
    }
    bool operator>(const std::string &str) const
    {
        return level > fromString(str);
    }
    bool operator>(int val) const
    {
        return level > static_cast<Level>(val);
    }
    bool operator>=(const Pressure &other) const
    {
        return level >= other.level;
    }
    bool operator>=(const std::string &str) const
    {
        return level >= fromString(str);
    }
    bool operator>=(int val) const
    {
        return level >= static_cast<Level>(val);
    }
    friend std::ostream &operator<<(std::ostream &os, const Pressure &p)
    {
        switch (p.level) {
            case Level::LOW: os << "low"; break;
            case Level::MEDIUM: os << "medium"; break;
            case Level::HIGH: os << "high"; break;
            case Level::FULL: os << "full"; break;
            default: os << "error"; break;
        }
        return os;
    }
};




} // Namespace Types
} // Namespace MX


#endif
