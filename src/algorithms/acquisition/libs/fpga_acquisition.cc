/*!
 * \file fpga_acquisition.cc
 * \brief Highly optimized FPGA vector correlator class
 * \authors <ul>
 *          <li> Marc Majoral, 2019. mmajoral(at)cttc.cat
 *          </ul>
 *
 * Class that controls and executes a highly optimized acquisition HW
 * accelerator in the FPGA
 *
 * -------------------------------------------------------------------------
 *
 * Copyright (C) 2010-2019  (see AUTHORS file for a list of contributors)
 *
 * GNSS-SDR is a software defined Global Navigation
 *          Satellite Systems receiver
 *
 * This file is part of GNSS-SDR.
 *
 * GNSS-SDR is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * GNSS-SDR is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNSS-SDR. If not, see <https://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------------
 */

#include "fpga_acquisition.h"
#include "GPS_L1_CA.h"     // for GPS_TWO_PI
#include <glog/logging.h>  // for LOG
#include <cmath>           // for log2
#include <fcntl.h>         // libraries used by the GIPO
#include <iostream>        // for operator<<
#include <sys/mman.h>      // libraries used by the GIPO
#include <unistd.h>        // for write, close, read, ssize_t
#include <utility>         // for move


// FPGA register parameters
#define PAGE_SIZE 0x10000                     // default page size for the multicorrelator memory map
#define MAX_PHASE_STEP_RAD 0.999999999534339  // 1 - pow(2,-31);
#define RESET_ACQUISITION 2                   // command to reset the multicorrelator
#define LAUNCH_ACQUISITION 1                  // command to launch the multicorrelator
#define TEST_REG_SANITY_CHECK 0x55AA          // value to check the presence of the test register (to detect the hw)
#define LOCAL_CODE_CLEAR_MEM 0x10000000       // command to clear the internal memory of the multicorrelator
#define MEM_LOCAL_CODE_WR_ENABLE 0x0C000000   // command to enable the ENA and WR pins of the internal memory of the multicorrelator
#define POW_2_2 4                             // 2^2 (used for the conversion of floating point numbers to integers)
#define POW_2_29 536870912                    // 2^29 (used for the conversion of floating point numbers to integers)
#define SELECT_LSB 0x00FF                     // value to select the least significant byte
#define SELECT_MSB 0XFF00                     // value to select the most significant byte
#define SELECT_16_BITS 0xFFFF                 // value to select 16 bits
#define SHL_8_BITS 256                        // value used to shift a value 8 bits to the left
#define SELECT_LSBits 0x000003FF              // Select the 10 LSbits out of a 20-bit word
#define SELECT_MSBbits 0x000FFC00             // Select the 10 MSbits out of a 20-bit word
#define SELECT_ALL_CODE_BITS 0x000FFFFF       // Select a 20 bit word
#define SHL_CODE_BITS 1024                    // shift left by 10 bits

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(exp)              \
    ({                                       \
        decltype(exp) _rc;                   \
        do                                   \
            {                                \
                _rc = (exp);                 \
            }                                \
        while (_rc == -1 && errno == EINTR); \
        _rc;                                 \
    })
#endif


Fpga_Acquisition::Fpga_Acquisition(std::string device_name,
    uint32_t nsamples,
    uint32_t doppler_max,
    uint32_t nsamples_total,
    int64_t fs_in,
    uint32_t sampled_ms __attribute__((unused)),
    uint32_t select_queue,
    lv_16sc_t *all_fft_codes,
    uint32_t excludelimit)
{
    uint32_t vector_length = nsamples_total;

    // initial values
    d_device_name = std::move(device_name);
    d_fs_in = fs_in;
    d_vector_length = vector_length;
    d_excludelimit = excludelimit;
    d_nsamples = nsamples;  // number of samples not including padding
    d_select_queue = select_queue;
    d_nsamples_total = nsamples_total;
    d_doppler_max = doppler_max;
    d_doppler_step = 0;
    d_fd = 0;              // driver descriptor
    d_map_base = nullptr;  // driver memory map
    d_all_fft_codes = all_fft_codes;

    Fpga_Acquisition::reset_acquisition();
    Fpga_Acquisition::open_device();
    Fpga_Acquisition::fpga_acquisition_test_register();
    Fpga_Acquisition::close_device();

    d_PRN = 0;
    DLOG(INFO) << "Acquisition FPGA class created";
}


Fpga_Acquisition::~Fpga_Acquisition() = default;


bool Fpga_Acquisition::init()
{
    return true;
}


bool Fpga_Acquisition::set_local_code(uint32_t PRN)
{
    // select the code with the chosen PRN
    d_PRN = PRN;
    return true;
}


void Fpga_Acquisition::write_local_code()
{
    Fpga_Acquisition::fpga_configure_acquisition_local_code(
        &d_all_fft_codes[d_nsamples_total * (d_PRN - 1)]);
}


void Fpga_Acquisition::open_device()
{
    // open communication with HW accelerator
    if ((d_fd = open(d_device_name.c_str(), O_RDWR | O_SYNC)) == -1)
        {
            LOG(WARNING) << "Cannot open deviceio" << d_device_name;
            std::cout << "Acq: cannot open deviceio" << d_device_name << std::endl;
        }
    d_map_base = reinterpret_cast<volatile uint32_t *>(mmap(nullptr, PAGE_SIZE,
        PROT_READ | PROT_WRITE, MAP_SHARED, d_fd, 0));

    if (d_map_base == reinterpret_cast<void *>(-1))
        {
            LOG(WARNING) << "Cannot map the FPGA acquisition module into user memory";
            std::cout << "Acq: cannot map deviceio" << d_device_name << std::endl;
        }
}


bool Fpga_Acquisition::free()
{
    return true;
}


void Fpga_Acquisition::fpga_acquisition_test_register()
{
    // sanity check : check test register
    uint32_t writeval = TEST_REG_SANITY_CHECK;
    uint32_t readval;

    // write value to test register
    d_map_base[15] = writeval;
    // read value from test register
    readval = d_map_base[15];

    if (writeval != readval)
        {
            LOG(WARNING) << "Acquisition test register sanity check failed";
        }
    else
        {
            LOG(INFO) << "Acquisition test register sanity check success!";
        }
}


void Fpga_Acquisition::fpga_configure_acquisition_local_code(lv_16sc_t fft_local_code[])
{
    uint32_t local_code;
    uint32_t k, tmp, tmp2;
    uint32_t fft_data;

    d_map_base[9] = LOCAL_CODE_CLEAR_MEM;
    // write local code
    for (k = 0; k < d_vector_length; k++)
        {
            tmp = fft_local_code[k].real();
            tmp2 = fft_local_code[k].imag();

            local_code = (tmp & SELECT_LSBits) | ((tmp2 * SHL_CODE_BITS) & SELECT_MSBbits);  // put together the real part and the imaginary part
            fft_data = local_code & SELECT_ALL_CODE_BITS;
            d_map_base[6] = fft_data;
        }
}


void Fpga_Acquisition::run_acquisition(void)
{
    // enable interrupts
    int32_t reenable = 1;
    int32_t disable_int = 0;
    ssize_t nbytes = TEMP_FAILURE_RETRY(write(d_fd, reinterpret_cast<void *>(&reenable), sizeof(int32_t)));
    if (nbytes != sizeof(int32_t))
        {
            std::cerr << "Error enabling run in the FPGA." << std::endl;
        }

    // launch the acquisition process
    d_map_base[8] = LAUNCH_ACQUISITION;  // writing a 1 to reg 8 launches the acquisition process
    int32_t irq_count;
    ssize_t nb;

    // wait for interrupt
    nb = read(d_fd, &irq_count, sizeof(irq_count));
    if (nb != sizeof(irq_count))
        {
            std::cout << "acquisition module Read failed to retrieve 4 bytes!" << std::endl;
            std::cout << "acquisition module Interrupt number " << irq_count << std::endl;
        }

    nbytes = TEMP_FAILURE_RETRY(write(d_fd, reinterpret_cast<void *>(&disable_int), sizeof(int32_t)));
    if (nbytes != sizeof(int32_t))
        {
            std::cerr << "Error disabling interruptions in the FPGA." << std::endl;
        }
}


void Fpga_Acquisition::set_block_exp(uint32_t total_block_exp)
{
    d_map_base[11] = total_block_exp;
}


void Fpga_Acquisition::set_doppler_sweep(uint32_t num_sweeps)
{
    float phase_step_rad_real;
    float phase_step_rad_int_temp;
    int32_t phase_step_rad_int;
    auto doppler = static_cast<int32_t>(-d_doppler_max);
    float phase_step_rad = GPS_TWO_PI * (doppler) / static_cast<float>(d_fs_in);
    // The doppler step can never be outside the range -pi to +pi, otherwise there would be aliasing
    // The FPGA expects phase_step_rad between -1 (-pi) to +1 (+pi)
    // The FPGA also expects the phase to be negative since it produces cos(x) -j*sin(x)
    phase_step_rad_real = phase_step_rad / (GPS_TWO_PI / 2);

    // avoid saturation of the fixed point representation in the fpga
    // (only the positive value can saturate due to the 2's complement representation)
    if (phase_step_rad_real >= 1.0)
        {
            phase_step_rad_real = MAX_PHASE_STEP_RAD;
        }
    phase_step_rad_int_temp = phase_step_rad_real * POW_2_2;                          // * 2^2
    phase_step_rad_int = static_cast<int32_t>(phase_step_rad_int_temp * (POW_2_29));  // * 2^29 (in total it makes x2^31 in two steps to avoid the warnings
    d_map_base[3] = phase_step_rad_int;

    // repeat the calculation with the doppler step
    doppler = static_cast<int32_t>(d_doppler_step);
    phase_step_rad = GPS_TWO_PI * (doppler) / static_cast<float>(d_fs_in);
    phase_step_rad_real = phase_step_rad / (GPS_TWO_PI / 2);
    if (phase_step_rad_real >= 1.0)
        {
            phase_step_rad_real = MAX_PHASE_STEP_RAD;
        }
    phase_step_rad_int_temp = phase_step_rad_real * POW_2_2;                          // * 2^2
    phase_step_rad_int = static_cast<int32_t>(phase_step_rad_int_temp * (POW_2_29));  // * 2^29 (in total it makes x2^31 in two steps to avoid the warnings
    d_map_base[4] = phase_step_rad_int;
    d_map_base[5] = num_sweeps;
}


void Fpga_Acquisition::configure_acquisition()
{
    Fpga_Acquisition::open_device();

    d_map_base[0] = d_select_queue;
    d_map_base[1] = d_vector_length;
    d_map_base[2] = d_nsamples;
    d_map_base[7] = static_cast<int32_t>(log2(static_cast<float>(d_vector_length)));  // log2 FFTlength
    d_map_base[12] = d_excludelimit;
}


void Fpga_Acquisition::set_phase_step(uint32_t doppler_index)
{
    float phase_step_rad_real;
    float phase_step_rad_int_temp;
    int32_t phase_step_rad_int;
    int32_t doppler = -static_cast<int32_t>(d_doppler_max) + d_doppler_step * doppler_index;
    float phase_step_rad = GPS_TWO_PI * (doppler) / static_cast<float>(d_fs_in);
    // The doppler step can never be outside the range -pi to +pi, otherwise there would be aliasing
    // The FPGA expects phase_step_rad between -1 (-pi) to +1 (+pi)
    // The FPGA also expects the phase to be negative since it produces cos(x) -j*sin(x)
    // while the gnss-sdr software (volk_gnsssdr_s32f_sincos_32fc) generates cos(x) + j*sin(x)
    phase_step_rad_real = phase_step_rad / (GPS_TWO_PI / 2);
    // avoid saturation of the fixed point representation in the fpga
    // (only the positive value can saturate due to the 2's complement representation)
    if (phase_step_rad_real >= 1.0)
        {
            phase_step_rad_real = MAX_PHASE_STEP_RAD;
        }
    phase_step_rad_int_temp = phase_step_rad_real * POW_2_2;                          // * 2^2
    phase_step_rad_int = static_cast<int32_t>(phase_step_rad_int_temp * (POW_2_29));  // * 2^29 (in total it makes x2^31 in two steps to avoid the warnings
    d_map_base[3] = phase_step_rad_int;
}


void Fpga_Acquisition::read_acquisition_results(uint32_t *max_index,
    float *firstpeak, float *secondpeak, uint64_t *initial_sample, float *power_sum, uint32_t *doppler_index, uint32_t *total_blk_exp)
{
    uint64_t initial_sample_tmp = 0;
    uint32_t readval = 0;
    uint64_t readval_long = 0;
    uint64_t readval_long_shifted = 0;

    readval = d_map_base[1];
    initial_sample_tmp = readval;

    readval_long = d_map_base[2];
    readval_long_shifted = readval_long << 32;  // 2^32

    initial_sample_tmp = initial_sample_tmp + readval_long_shifted;  // 2^32
    *initial_sample = initial_sample_tmp;

    readval = d_map_base[3];
    *firstpeak = static_cast<float>(readval);

    readval = d_map_base[4];
    *secondpeak = static_cast<float>(readval);

    readval = d_map_base[5];
    *max_index = readval;

    *power_sum = 0;

    readval = d_map_base[8];
    *total_blk_exp = readval;

    readval = d_map_base[7];  // read doppler index -- this read releases the interrupt line
    *doppler_index = readval;

    readval = d_map_base[15];  // read dummy

    Fpga_Acquisition::close_device();
}


void Fpga_Acquisition::block_samples()
{
    d_map_base[14] = 1;  // block the samples
}


void Fpga_Acquisition::unblock_samples()
{
    d_map_base[14] = 0;  // unblock the samples
}


void Fpga_Acquisition::close_device()
{
    auto *aux = const_cast<uint32_t *>(d_map_base);
    if (munmap(static_cast<void *>(aux), PAGE_SIZE) == -1)
        {
            std::cout << "Failed to unmap memory uio" << std::endl;
        }
    close(d_fd);
}


void Fpga_Acquisition::reset_acquisition(void)
{
    Fpga_Acquisition::open_device();
    d_map_base[8] = RESET_ACQUISITION;  // writing a 2 to d_map_base[8] resets the multicorrelator
    Fpga_Acquisition::close_device();
}


// this function is only used for the unit tests
void Fpga_Acquisition::read_fpga_total_scale_factor(uint32_t *total_scale_factor, uint32_t *fw_scale_factor)
{
    uint32_t readval = 0;
    readval = d_map_base[8];
    *total_scale_factor = readval;

    //readval = d_map_base[8];
    *fw_scale_factor = 0;
}

void Fpga_Acquisition::read_result_valid(uint32_t *result_valid)
{
    uint32_t readval = 0;
    readval = d_map_base[0];
    *result_valid = readval;
}
