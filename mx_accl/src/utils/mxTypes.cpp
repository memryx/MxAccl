// Copyright (c) 2025-2026 MemryX
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <memx/accl/utils/mxTypes.h>

using namespace MX::Types;

// Constructor
MX::Types::ShapeVector::ShapeVector() : shape(4, 1) {} // Initialize shape with 4 elements, all initialized to 0
// Constructor
MX::Types::ShapeVector::ShapeVector(int64_t h, int64_t w, int64_t z, int64_t c)
{
    this->shape.reserve(4);
    this->h = h;
    this->shape.push_back(h); // = h;
    this->w = w;
    this->shape.push_back(w); // = w;
    this->z = z;
    this->shape.push_back(z); // = z;
    this->c = c;
    this->shape.push_back(c); // = c;
    // std::cout<<"this shape size = "<<this->shape.size();
}
// Constructor
MX::Types::ShapeVector::ShapeVector(int size) : shape(size, 1),
    size_(size) {} // Initialize shape with 4 elements, all initialized to 0


// Overload [] operator
// const int64_t& MX::Types::ShapeVector::operator[](int64_t index) const {
//     if (index < 4) {
//         return shape[index];
//     } else {
//         std::cerr << "Error: Index out of range." << std::endl;
//         throw std::runtime_error("Error: Index out of range." );
//     }
// }

// Overload [] operator
int64_t &MX::Types::ShapeVector::operator[](int64_t index)
{
    if (index < size_) {
        return shape[index];
    }
    else {
        std::cerr << "Error: Index out of range." << std::endl;
        throw std::runtime_error("Error: Index out of range." );
    }
}

std::vector<int64_t> MX::Types::ShapeVector::chfirst_shape()
{
    std::vector chfirst_shape{this->c, this->h, this->w, this->z};
    return chfirst_shape;
}

std::vector<int64_t> MX::Types::ShapeVector::chlast_shape()
{
    // std::vector chfirst_shape{this->h, this->w, this->z, this->c};
    return shape;
}

// Function to return a pointer to shape vector
int64_t* MX::Types::ShapeVector::data()
{
    return shape.data();
}

// Function to return the size of the internal vector
int64_t MX::Types::ShapeVector::size() const
{
    return shape.size();
}

void MX::Types::ShapeVector::set_ch_first()
{
    shape = this->chfirst_shape();
}

bool MX::Types::ShapeVector::operator==(const ShapeVector& other) const
{
    if (this->size() != other.size()) {
        return false;
    }
    for (int i = 0; i < this->size(); ++i) {
        if (this->shape[i] != other.shape[i]) {
            return false;
        }
    }
    return true;
}

bool MX::Types::ShapeVector::operator!=(const ShapeVector& other) const
{
    return !(*this == other);
}

std::string MX::Types::ShapeVector::to_string() const
{
    std::string result = "[";
    for (size_t i = 0; i < shape.size(); ++i) {
        result += std::to_string(shape[i]);
        if (i < shape.size() - 1) {
            result += ", ";
        }
    }
    result += "]";
    return result;
}


MxVoltageOption MX::Types::getVoltageFromFrequency(MxFrequencyOption freq)
{
    switch (freq) {
        case MxFrequencyOption::FREQ_200MHz:
        case MxFrequencyOption::FREQ_225MHz:
        case MxFrequencyOption::FREQ_250MHz:
        case MxFrequencyOption::FREQ_275MHz:
            return MxVoltageOption::VOLT_670mV;

        case MxFrequencyOption::FREQ_300MHz:
        case MxFrequencyOption::FREQ_325MHz:
            return MxVoltageOption::VOLT_675mV;


        case MxFrequencyOption::FREQ_350MHz:
        case MxFrequencyOption::FREQ_375MHz:
            return MxVoltageOption::VOLT_680mV;

        case MxFrequencyOption::FREQ_400MHz:
        case MxFrequencyOption::FREQ_425MHz:
        case MxFrequencyOption::FREQ_450MHz:
            return MxVoltageOption::VOLT_685mV;

        case MxFrequencyOption::FREQ_475MHz:
        case MxFrequencyOption::FREQ_500MHz:
        case MxFrequencyOption::FREQ_525MHz:
            return MxVoltageOption::VOLT_690mV;

        case MxFrequencyOption::FREQ_550MHz:
        case MxFrequencyOption::FREQ_575MHz:
            return MxVoltageOption::VOLT_695mV;

        case MxFrequencyOption::FREQ_600MHz:
            return MxVoltageOption::VOLT_700mV;

        case MxFrequencyOption::FREQ_625MHz:
            return MxVoltageOption::VOLT_705mV;

        case MxFrequencyOption::FREQ_650MHz:
            return MxVoltageOption::VOLT_710mV;

        case MxFrequencyOption::FREQ_675MHz:
            return MxVoltageOption::VOLT_720mV;

        case MxFrequencyOption::FREQ_700MHz:
            return MxVoltageOption::VOLT_725mV;

        case MxFrequencyOption::FREQ_725MHz:
            return MxVoltageOption::VOLT_740mV;

        case MxFrequencyOption::FREQ_750MHz:
            return MxVoltageOption::VOLT_745mV;

        case MxFrequencyOption::FREQ_775MHz:
            return MxVoltageOption::VOLT_750mV;

        case MxFrequencyOption::FREQ_800MHz:
            return MxVoltageOption::VOLT_760mV;

        case MxFrequencyOption::FREQ_825MHz:
            return MxVoltageOption::VOLT_770mV;

        case MxFrequencyOption::FREQ_850MHz:
            return MxVoltageOption::VOLT_780mV;

        case MxFrequencyOption::FREQ_875MHz:
            return MxVoltageOption::VOLT_790mV;

        case MxFrequencyOption::FREQ_900MHz:
            return MxVoltageOption::VOLT_800mV;

        case MxFrequencyOption::FREQ_925MHz:
            return MxVoltageOption::VOLT_815mV;

        case MxFrequencyOption::FREQ_950MHz:
            return MxVoltageOption::VOLT_820mV;

        case MxFrequencyOption::FREQ_975MHz:
            return MxVoltageOption::VOLT_835mV;

        case MxFrequencyOption::FREQ_1000MHz:
            return MxVoltageOption::VOLT_850mV;

        default:
            throw std::invalid_argument("Invalid frequency option.");
    }
}


std::string MX::Types::mxFrequencyOptionToString(MxFrequencyOption freq)
{
    // if 0, return "USE_CONF"
    // else return the number as string + MHz
    if (freq == MxFrequencyOption::FREQ_USE_CONF) {
        return "USE_CONF";
    }
    else {
        return std::to_string(static_cast<int>(freq)) + "MHz";
    }
}
