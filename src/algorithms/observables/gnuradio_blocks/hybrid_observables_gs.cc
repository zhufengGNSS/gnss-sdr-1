/*!
 * \file hybrid_observables_gs.cc
 * \brief Implementation of the observables computation block
 * \author Javier Arribas 2017. jarribas(at)cttc.es
 * \author Antonio Ramos  2018. antonio.ramos(at)cttc.es
 *
 * -------------------------------------------------------------------------
 *
 * Copyright (C) 2010-2018  (see AUTHORS file for a list of contributors)
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

#include "hybrid_observables_gs.h"
#include "GPS_L1_CA.h"       // for GPS_STARTOFFSET_MS, GPS_TWO_PI
#include "MATH_CONSTANTS.h"  // for SPEED_OF_LIGHT
#include "gnss_circular_deque.h"
#include "gnss_sdr_create_directory.h"
#include "gnss_synchro.h"
#include <boost/filesystem/path.hpp>
#include <glog/logging.h>
#include <gnuradio/io_signature.h>
#include <matio.h>
#include <cmath>      // for round
#include <cstdlib>    // for size_t, llabs
#include <exception>  // for exception
#include <iostream>   // for cerr, cout
#include <limits>     // for numeric_limits
#include <utility>    // for move


hybrid_observables_gs_sptr hybrid_observables_gs_make(unsigned int nchannels_in, unsigned int nchannels_out, bool dump, bool dump_mat, std::string dump_filename)
{
    return hybrid_observables_gs_sptr(new hybrid_observables_gs(nchannels_in, nchannels_out, dump, dump_mat, std::move(dump_filename)));
}


hybrid_observables_gs::hybrid_observables_gs(uint32_t nchannels_in,
    uint32_t nchannels_out,
    bool dump,
    bool dump_mat,
    std::string dump_filename) : gr::block("hybrid_observables_gs",
                                     gr::io_signature::make(nchannels_in, nchannels_in, sizeof(Gnss_Synchro)),
                                     gr::io_signature::make(nchannels_out, nchannels_out, sizeof(Gnss_Synchro)))
{
    d_dump = dump;
    d_dump_mat = dump_mat and d_dump;
    d_dump_filename = std::move(dump_filename);
    d_nchannels_out = nchannels_out;
    d_nchannels_in = nchannels_in;
    T_rx_clock_step_samples = 0U;
    d_gnss_synchro_history = new Gnss_circular_deque<Gnss_Synchro>(500, d_nchannels_out);

    // ############# ENABLE DATA FILE LOG #################
    if (d_dump)
        {
            std::string dump_path;
            // Get path
            if (d_dump_filename.find_last_of('/') != std::string::npos)
                {
                    std::string dump_filename_ = d_dump_filename.substr(d_dump_filename.find_last_of('/') + 1);
                    dump_path = d_dump_filename.substr(0, d_dump_filename.find_last_of('/'));
                    d_dump_filename = dump_filename_;
                }
            else
                {
                    dump_path = std::string(".");
                }
            if (d_dump_filename.empty())
                {
                    d_dump_filename = "observables.dat";
                }
            // remove extension if any
            if (d_dump_filename.substr(1).find_last_of('.') != std::string::npos)
                {
                    d_dump_filename = d_dump_filename.substr(0, d_dump_filename.find_last_of('.'));
                }
            d_dump_filename.append(".dat");
            d_dump_filename = dump_path + boost::filesystem::path::preferred_separator + d_dump_filename;
            // create directory
            if (!gnss_sdr_create_directory(dump_path))
                {
                    std::cerr << "GNSS-SDR cannot create dump file for the Observables block. Wrong permissions?" << std::endl;
                    d_dump = false;
                }
            d_dump_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
            try
                {
                    d_dump_file.open(d_dump_filename.c_str(), std::ios::out | std::ios::binary);
                    LOG(INFO) << "Observables dump enabled Log file: " << d_dump_filename.c_str();
                }
            catch (const std::ifstream::failure &e)
                {
                    LOG(WARNING) << "Exception opening observables dump file " << e.what();
                    d_dump = false;
                }
        }
    T_rx_TOW_ms = 0U;
    T_rx_TOW_offset_ms = 0U;
    T_rx_TOW_set = false;

    // rework
    d_Rx_clock_buffer.set_capacity(10);  // 10*20 ms = 200 ms of data in buffer
    d_Rx_clock_buffer.clear();           // Clear all the elements in the buffer
}


hybrid_observables_gs::~hybrid_observables_gs()
{
    delete d_gnss_synchro_history;
    if (d_dump_file.is_open())
        {
            try
                {
                    d_dump_file.close();
                }
            catch (const std::exception &ex)
                {
                    LOG(WARNING) << "Exception in destructor closing the dump file " << ex.what();
                }
        }
    if (d_dump_mat)
        {
            try
                {
                    save_matfile();
                }
            catch (const std::exception &ex)
                {
                    LOG(WARNING) << "Error saving the .mat file: " << ex.what();
                }
        }
}


int32_t hybrid_observables_gs::save_matfile()
{
    // READ DUMP FILE
    std::string dump_filename = d_dump_filename;
    std::ifstream::pos_type size;
    int32_t number_of_double_vars = 7;
    int32_t epoch_size_bytes = sizeof(double) * number_of_double_vars * d_nchannels_out;
    std::ifstream dump_file;
    std::cout << "Generating .mat file for " << dump_filename << std::endl;
    dump_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try
        {
            dump_file.open(dump_filename.c_str(), std::ios::binary | std::ios::ate);
        }
    catch (const std::ifstream::failure &e)
        {
            std::cerr << "Problem opening dump file:" << e.what() << std::endl;
            return 1;
        }
    // count number of epochs and rewind
    int64_t num_epoch = 0LL;
    if (dump_file.is_open())
        {
            size = dump_file.tellg();
            num_epoch = static_cast<int64_t>(size) / static_cast<int64_t>(epoch_size_bytes);
            dump_file.seekg(0, std::ios::beg);
        }
    else
        {
            return 1;
        }
    auto **RX_time = new double *[d_nchannels_out];
    auto **TOW_at_current_symbol_s = new double *[d_nchannels_out];
    auto **Carrier_Doppler_hz = new double *[d_nchannels_out];
    auto **Carrier_phase_cycles = new double *[d_nchannels_out];
    auto **Pseudorange_m = new double *[d_nchannels_out];
    auto **PRN = new double *[d_nchannels_out];
    auto **Flag_valid_pseudorange = new double *[d_nchannels_out];

    for (uint32_t i = 0; i < d_nchannels_out; i++)
        {
            RX_time[i] = new double[num_epoch];
            TOW_at_current_symbol_s[i] = new double[num_epoch];
            Carrier_Doppler_hz[i] = new double[num_epoch];
            Carrier_phase_cycles[i] = new double[num_epoch];
            Pseudorange_m[i] = new double[num_epoch];
            PRN[i] = new double[num_epoch];
            Flag_valid_pseudorange[i] = new double[num_epoch];
        }

    try
        {
            if (dump_file.is_open())
                {
                    for (int64_t i = 0; i < num_epoch; i++)
                        {
                            for (uint32_t chan = 0; chan < d_nchannels_out; chan++)
                                {
                                    dump_file.read(reinterpret_cast<char *>(&RX_time[chan][i]), sizeof(double));
                                    dump_file.read(reinterpret_cast<char *>(&TOW_at_current_symbol_s[chan][i]), sizeof(double));
                                    dump_file.read(reinterpret_cast<char *>(&Carrier_Doppler_hz[chan][i]), sizeof(double));
                                    dump_file.read(reinterpret_cast<char *>(&Carrier_phase_cycles[chan][i]), sizeof(double));
                                    dump_file.read(reinterpret_cast<char *>(&Pseudorange_m[chan][i]), sizeof(double));
                                    dump_file.read(reinterpret_cast<char *>(&PRN[chan][i]), sizeof(double));
                                    dump_file.read(reinterpret_cast<char *>(&Flag_valid_pseudorange[chan][i]), sizeof(double));
                                }
                        }
                }
            dump_file.close();
        }
    catch (const std::ifstream::failure &e)
        {
            std::cerr << "Problem reading dump file:" << e.what() << std::endl;
            for (uint32_t i = 0; i < d_nchannels_out; i++)
                {
                    delete[] RX_time[i];
                    delete[] TOW_at_current_symbol_s[i];
                    delete[] Carrier_Doppler_hz[i];
                    delete[] Carrier_phase_cycles[i];
                    delete[] Pseudorange_m[i];
                    delete[] PRN[i];
                    delete[] Flag_valid_pseudorange[i];
                }
            delete[] RX_time;
            delete[] TOW_at_current_symbol_s;
            delete[] Carrier_Doppler_hz;
            delete[] Carrier_phase_cycles;
            delete[] Pseudorange_m;
            delete[] PRN;
            delete[] Flag_valid_pseudorange;

            return 1;
        }

    auto *RX_time_aux = new double[d_nchannels_out * num_epoch];
    auto *TOW_at_current_symbol_s_aux = new double[d_nchannels_out * num_epoch];
    auto *Carrier_Doppler_hz_aux = new double[d_nchannels_out * num_epoch];
    auto *Carrier_phase_cycles_aux = new double[d_nchannels_out * num_epoch];
    auto *Pseudorange_m_aux = new double[d_nchannels_out * num_epoch];
    auto *PRN_aux = new double[d_nchannels_out * num_epoch];
    auto *Flag_valid_pseudorange_aux = new double[d_nchannels_out * num_epoch];
    uint32_t k = 0U;
    for (int64_t j = 0; j < num_epoch; j++)
        {
            for (uint32_t i = 0; i < d_nchannels_out; i++)
                {
                    RX_time_aux[k] = RX_time[i][j];
                    TOW_at_current_symbol_s_aux[k] = TOW_at_current_symbol_s[i][j];
                    Carrier_Doppler_hz_aux[k] = Carrier_Doppler_hz[i][j];
                    Carrier_phase_cycles_aux[k] = Carrier_phase_cycles[i][j];
                    Pseudorange_m_aux[k] = Pseudorange_m[i][j];
                    PRN_aux[k] = PRN[i][j];
                    Flag_valid_pseudorange_aux[k] = Flag_valid_pseudorange[i][j];
                    k++;
                }
        }

    // WRITE MAT FILE
    mat_t *matfp;
    matvar_t *matvar;
    std::string filename = d_dump_filename;
    if (filename.size() > 4)
        {
            filename.erase(filename.end() - 4, filename.end());
        }
    filename.append(".mat");
    matfp = Mat_CreateVer(filename.c_str(), nullptr, MAT_FT_MAT73);
    if (reinterpret_cast<int64_t *>(matfp) != nullptr)
        {
            size_t dims[2] = {static_cast<size_t>(d_nchannels_out), static_cast<size_t>(num_epoch)};
            matvar = Mat_VarCreate("RX_time", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims, RX_time_aux, MAT_F_DONT_COPY_DATA);
            Mat_VarWrite(matfp, matvar, MAT_COMPRESSION_ZLIB);  // or MAT_COMPRESSION_NONE
            Mat_VarFree(matvar);

            matvar = Mat_VarCreate("TOW_at_current_symbol_s", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims, TOW_at_current_symbol_s_aux, MAT_F_DONT_COPY_DATA);
            Mat_VarWrite(matfp, matvar, MAT_COMPRESSION_ZLIB);  // or MAT_COMPRESSION_NONE
            Mat_VarFree(matvar);

            matvar = Mat_VarCreate("Carrier_Doppler_hz", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims, Carrier_Doppler_hz_aux, MAT_F_DONT_COPY_DATA);
            Mat_VarWrite(matfp, matvar, MAT_COMPRESSION_ZLIB);  // or MAT_COMPRESSION_NONE
            Mat_VarFree(matvar);

            matvar = Mat_VarCreate("Carrier_phase_cycles", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims, Carrier_phase_cycles_aux, MAT_F_DONT_COPY_DATA);
            Mat_VarWrite(matfp, matvar, MAT_COMPRESSION_ZLIB);  // or MAT_COMPRESSION_NONE
            Mat_VarFree(matvar);

            matvar = Mat_VarCreate("Pseudorange_m", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims, Pseudorange_m_aux, MAT_F_DONT_COPY_DATA);
            Mat_VarWrite(matfp, matvar, MAT_COMPRESSION_ZLIB);  // or MAT_COMPRESSION_NONE
            Mat_VarFree(matvar);

            matvar = Mat_VarCreate("PRN", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims, PRN_aux, MAT_F_DONT_COPY_DATA);
            Mat_VarWrite(matfp, matvar, MAT_COMPRESSION_ZLIB);  // or MAT_COMPRESSION_NONE
            Mat_VarFree(matvar);

            matvar = Mat_VarCreate("Flag_valid_pseudorange", MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims, Flag_valid_pseudorange_aux, MAT_F_DONT_COPY_DATA);
            Mat_VarWrite(matfp, matvar, MAT_COMPRESSION_ZLIB);  // or MAT_COMPRESSION_NONE
            Mat_VarFree(matvar);
        }
    Mat_Close(matfp);

    for (uint32_t i = 0; i < d_nchannels_out; i++)
        {
            delete[] RX_time[i];
            delete[] TOW_at_current_symbol_s[i];
            delete[] Carrier_Doppler_hz[i];
            delete[] Carrier_phase_cycles[i];
            delete[] Pseudorange_m[i];
            delete[] PRN[i];
            delete[] Flag_valid_pseudorange[i];
        }
    delete[] RX_time;
    delete[] TOW_at_current_symbol_s;
    delete[] Carrier_Doppler_hz;
    delete[] Carrier_phase_cycles;
    delete[] Pseudorange_m;
    delete[] PRN;
    delete[] Flag_valid_pseudorange;

    delete[] RX_time_aux;
    delete[] TOW_at_current_symbol_s_aux;
    delete[] Carrier_Doppler_hz_aux;
    delete[] Carrier_phase_cycles_aux;
    delete[] Pseudorange_m_aux;
    delete[] PRN_aux;
    delete[] Flag_valid_pseudorange_aux;
    return 0;
}


double hybrid_observables_gs::compute_T_rx_s(const Gnss_Synchro &a)
{
    return ((static_cast<double>(a.Tracking_sample_counter) + a.Code_phase_samples) / static_cast<double>(a.fs));
}


bool hybrid_observables_gs::interp_trk_obs(Gnss_Synchro &interpolated_obs, const uint32_t &ch, const uint64_t &rx_clock)
{
    int32_t nearest_element = -1;
    int64_t abs_diff;
    int64_t old_abs_diff = std::numeric_limits<int64_t>::max();
    for (uint32_t i = 0; i < d_gnss_synchro_history->size(ch); i++)
        {
            abs_diff = llabs(static_cast<int64_t>(rx_clock) - static_cast<int64_t>(d_gnss_synchro_history->at(ch, i).Tracking_sample_counter));
            if (old_abs_diff > abs_diff)
                {
                    old_abs_diff = abs_diff;
                    nearest_element = static_cast<int32_t>(i);
                }
        }

    if (nearest_element != -1 and nearest_element != static_cast<int32_t>(d_gnss_synchro_history->size(ch)))
        {
            if ((static_cast<double>(old_abs_diff) / static_cast<double>(d_gnss_synchro_history->at(ch, nearest_element).fs)) < 0.02)
                {
                    int32_t neighbor_element;
                    if (rx_clock > d_gnss_synchro_history->at(ch, nearest_element).Tracking_sample_counter)
                        {
                            neighbor_element = nearest_element + 1;
                        }
                    else
                        {
                            neighbor_element = nearest_element - 1;
                        }
                    if (neighbor_element < static_cast<int32_t>(d_gnss_synchro_history->size(ch)) and neighbor_element >= 0)
                        {
                            int32_t t1_idx;
                            int32_t t2_idx;
                            if (rx_clock > d_gnss_synchro_history->at(ch, nearest_element).Tracking_sample_counter)
                                {
                                    //std::cout << "S1= " << d_gnss_synchro_history->at(ch, nearest_element).Tracking_sample_counter
                                    //          << " Si=" << rx_clock << " S2=" << d_gnss_synchro_history->at(ch, neighbor_element).Tracking_sample_counter << std::endl;
                                    t1_idx = nearest_element;
                                    t2_idx = neighbor_element;
                                }
                            else
                                {
                                    //std::cout << "inv S1= " << d_gnss_synchro_history->at(ch, neighbor_element).Tracking_sample_counter
                                    //          << " Si=" << rx_clock << " S2=" << d_gnss_synchro_history->at(ch, nearest_element).Tracking_sample_counter << std::endl;
                                    t1_idx = neighbor_element;
                                    t2_idx = nearest_element;
                                }

                            // 1st: copy the nearest gnss_synchro data for that channel
                            interpolated_obs = d_gnss_synchro_history->at(ch, nearest_element);

                            // 2nd: Linear interpolation: y(t) = y(t1) + (y(t2) - y(t1)) * (t - t1) / (t2 - t1)

                            double T_rx_s = static_cast<double>(rx_clock) / static_cast<double>(interpolated_obs.fs);

                            double time_factor = (T_rx_s - d_gnss_synchro_history->at(ch, t1_idx).RX_time) /
                                                 (d_gnss_synchro_history->at(ch, t2_idx).RX_time -
                                                     d_gnss_synchro_history->at(ch, t1_idx).RX_time);

                            // CARRIER PHASE INTERPOLATION
                            interpolated_obs.Carrier_phase_rads = d_gnss_synchro_history->at(ch, t1_idx).Carrier_phase_rads + (d_gnss_synchro_history->at(ch, t2_idx).Carrier_phase_rads - d_gnss_synchro_history->at(ch, t1_idx).Carrier_phase_rads) * time_factor;
                            // CARRIER DOPPLER INTERPOLATION
                            interpolated_obs.Carrier_Doppler_hz = d_gnss_synchro_history->at(ch, t1_idx).Carrier_Doppler_hz + (d_gnss_synchro_history->at(ch, t2_idx).Carrier_Doppler_hz - d_gnss_synchro_history->at(ch, t1_idx).Carrier_Doppler_hz) * time_factor;
                            // TOW INTERPOLATION
                            interpolated_obs.interp_TOW_ms = static_cast<double>(d_gnss_synchro_history->at(ch, t1_idx).TOW_at_current_symbol_ms) + (static_cast<double>(d_gnss_synchro_history->at(ch, t2_idx).TOW_at_current_symbol_ms) - static_cast<double>(d_gnss_synchro_history->at(ch, t1_idx).TOW_at_current_symbol_ms)) * time_factor;
                            //
                            // std::cout << "Rx samplestamp: " << T_rx_s << " Channel " << ch << " interp buff idx " << nearest_element
                            //           << " ,diff: " << old_abs_diff << " samples (" << static_cast<double>(old_abs_diff) / static_cast<double>(d_gnss_synchro_history->at(ch, nearest_element).fs) << " s)\n";
                            return true;
                        }
                    return false;
                }
            // std::cout << "ALERT: Channel " << ch << " interp buff idx " << nearest_element
            //           << " ,diff: " << old_abs_diff << " samples (" << static_cast<double>(old_abs_diff) / static_cast<double>(d_gnss_synchro_history->at(ch, nearest_element).fs) << " s)\n";
            // usleep(1000);
        }
    return false;
}


void hybrid_observables_gs::forecast(int noutput_items __attribute__((unused)), gr_vector_int &ninput_items_required)
{
    for (int32_t n = 0; n < static_cast<int32_t>(d_nchannels_in) - 1; n++)
        {
            ninput_items_required[n] = 0;
        }
    // last input channel is the sample counter, triggered each ms
    ninput_items_required[d_nchannels_in - 1] = 1;
}


void hybrid_observables_gs::update_TOW(const std::vector<Gnss_Synchro> &data)
{
    //1. Set the TOW using the minimum TOW in the observables.
    //   this will be the receiver time.
    //2. If the TOW is set, it must be incremented by the desired receiver time step.
    //   the time step must match the observables timer block (connected to the las input channel)
    std::vector<Gnss_Synchro>::const_iterator it;
    //    if (!T_rx_TOW_set)
    //        {
    //uint32_t TOW_ref = std::numeric_limits<uint32_t>::max();
    uint32_t TOW_ref = 0U;
    for (it = data.cbegin(); it != data.cend(); it++)
        {
            if (it->Flag_valid_word)
                {
                    if (it->TOW_at_current_symbol_ms > TOW_ref)
                        {
                            TOW_ref = it->TOW_at_current_symbol_ms;
                            T_rx_TOW_set = true;
                        }
                }
        }
    T_rx_TOW_ms = TOW_ref;
    //}
    //    else
    //        {
    //            T_rx_TOW_ms += T_rx_step_ms;
    //            //todo: check what happens during the week rollover
    //            if (T_rx_TOW_ms >= 604800000)
    //                {
    //                    T_rx_TOW_ms = T_rx_TOW_ms % 604800000;
    //                }
    //        }
    //    std::cout << "T_rx_TOW_ms: " << T_rx_TOW_ms << std::endl;
}


void hybrid_observables_gs::compute_pranges(std::vector<Gnss_Synchro> &data)
{
    std::vector<Gnss_Synchro>::iterator it;
    for (it = data.begin(); it != data.end(); it++)
        {
            if (it->Flag_valid_word)
                {
                    double traveltime_s = (static_cast<double>(T_rx_TOW_ms) - it->interp_TOW_ms + GPS_STARTOFFSET_MS) / 1000.0;
                    //todo: check what happens during the week rollover (TOW rollover at 604800000s)
                    it->RX_time = (static_cast<double>(T_rx_TOW_ms) + GPS_STARTOFFSET_MS) / 1000.0;
                    it->Pseudorange_m = traveltime_s * SPEED_OF_LIGHT;
                    it->Flag_valid_pseudorange = true;
                    // debug code
                    // std::cout.precision(17);
                    // std::cout << "[" << it->Channel_ID << "] interp_TOW_ms: " << it->interp_TOW_ms << std::endl;
                    // std::cout << "[" << it->Channel_ID << "] Diff T_rx_TOW_ms - interp_TOW_ms: " << static_cast<double>(T_rx_TOW_ms) - it->interp_TOW_ms << std::endl;
                    // std::cout << "[" << it->Channel_ID << "] Pseudorange_m: " << it->Pseudorange_m << std::endl;
                }
        }
    // usleep(1000);
}


int hybrid_observables_gs::general_work(int noutput_items __attribute__((unused)),
    gr_vector_int &ninput_items, gr_vector_const_void_star &input_items,
    gr_vector_void_star &output_items)
{
    const auto **in = reinterpret_cast<const Gnss_Synchro **>(&input_items[0]);
    auto **out = reinterpret_cast<Gnss_Synchro **>(&output_items[0]);

    // Push receiver clock into history buffer (connected to the last of the input channels)
    // The clock buffer gives time to the channels to compute the tracking observables
    if (ninput_items[d_nchannels_in - 1] > 0)
        {
            d_Rx_clock_buffer.push_back(in[d_nchannels_in - 1][0].Tracking_sample_counter);
            if (T_rx_clock_step_samples == 0)
                {
                    T_rx_clock_step_samples = std::round(static_cast<double>(in[d_nchannels_in - 1][0].fs) * 1e-3);  // 1 ms
                    LOG(INFO) << "Observables clock step samples set to " << T_rx_clock_step_samples;
                }

            // Consume one item from the clock channel (last of the input channels)
            consume(d_nchannels_in - 1, 1);
        }

    // Push the tracking observables into buffers to allow the observable interpolation at the desired Rx clock
    for (uint32_t n = 0; n < d_nchannels_out; n++)
        {
            // Push the valid tracking Gnss_Synchros to their corresponding deque
            for (int32_t m = 0; m < ninput_items[n]; m++)
                {
                    if (in[n][m].Flag_valid_word)
                        {
                            if (d_gnss_synchro_history->size(n) > 0)
                                {
                                    // Check if the last Gnss_Synchro comes from the same satellite as the previous ones
                                    if (d_gnss_synchro_history->front(n).PRN != in[n][m].PRN)
                                        {
                                            d_gnss_synchro_history->clear(n);
                                        }
                                }
                            d_gnss_synchro_history->push_back(n, in[n][m]);
                            d_gnss_synchro_history->back(n).RX_time = compute_T_rx_s(in[n][m]);
                        }
                }
            consume(n, ninput_items[n]);
        }

    if (d_Rx_clock_buffer.size() == d_Rx_clock_buffer.capacity())
        {
            std::vector<Gnss_Synchro> epoch_data;
            int32_t n_valid = 0;
            for (uint32_t n = 0; n < d_nchannels_out; n++)
                {
                    Gnss_Synchro interpolated_gnss_synchro{};
                    if (!interp_trk_obs(interpolated_gnss_synchro, n, d_Rx_clock_buffer.front() + T_rx_TOW_offset_ms * T_rx_clock_step_samples))
                        {
                            // Produce an empty observation
                            interpolated_gnss_synchro = Gnss_Synchro();
                            interpolated_gnss_synchro.Flag_valid_pseudorange = false;
                            interpolated_gnss_synchro.Flag_valid_word = false;
                            interpolated_gnss_synchro.Flag_valid_acquisition = false;
                            interpolated_gnss_synchro.fs = 0;
                            interpolated_gnss_synchro.Channel_ID = n;
                        }
                    else
                        {
                            n_valid++;
                        }
                    epoch_data.push_back(interpolated_gnss_synchro);
                }
            if (n_valid > 0)
                {
                    update_TOW(epoch_data);
                    int tow_inc_loop_count = 0;
                    while (T_rx_TOW_ms % 20 != 0 and tow_inc_loop_count < 20)
                        {
                            tow_inc_loop_count++;
                            T_rx_TOW_offset_ms++;
                            T_rx_TOW_offset_ms = T_rx_TOW_offset_ms % 20;
                            //check if effectively the receiver TOW is now multiple of 20 ms
                            n_valid = 0;
                            epoch_data.clear();
                            for (uint32_t n = 0; n < d_nchannels_out; n++)
                                {
                                    Gnss_Synchro interpolated_gnss_synchro{};
                                    if (!interp_trk_obs(interpolated_gnss_synchro, n, d_Rx_clock_buffer.front() + T_rx_TOW_offset_ms * T_rx_clock_step_samples))
                                        {
                                            // Produce an empty observation
                                            interpolated_gnss_synchro = Gnss_Synchro();
                                            interpolated_gnss_synchro.Flag_valid_pseudorange = false;
                                            interpolated_gnss_synchro.Flag_valid_word = false;
                                            interpolated_gnss_synchro.Flag_valid_acquisition = false;
                                            interpolated_gnss_synchro.fs = 0;
                                            interpolated_gnss_synchro.Channel_ID = n;
                                        }
                                    else
                                        {
                                            n_valid++;
                                        }
                                    epoch_data.push_back(interpolated_gnss_synchro);
                                }
                            update_TOW(epoch_data);
                            // debug code:
                            // if (T_rx_TOW_ms % 20 != 0)
                            //     {
                            //         std::cout << "Warning: RX TOW is not multiple of 20 ms\n";
                            //     }
                            // std::cout << "T_rx_TOW_ms=" << T_rx_TOW_ms << " T_rx_TOW_offset_ms=" << T_rx_TOW_offset_ms << " ->" << T_rx_TOW_ms % 20 << std::endl;
                        }
                }

            if (n_valid > 0)
                {
                    compute_pranges(epoch_data);
                }

            for (uint32_t n = 0; n < d_nchannels_out; n++)
                {
                    out[n][0] = epoch_data.at(n);
                }
            if (d_dump)
                {
                    // MULTIPLEXED FILE RECORDING - Record results to file
                    try
                        {
                            double tmp_double;
                            for (uint32_t i = 0; i < d_nchannels_out; i++)
                                {
                                    tmp_double = out[i][0].RX_time;
                                    d_dump_file.write(reinterpret_cast<char *>(&tmp_double), sizeof(double));
                                    tmp_double = out[i][0].interp_TOW_ms / 1000.0;
                                    d_dump_file.write(reinterpret_cast<char *>(&tmp_double), sizeof(double));
                                    tmp_double = out[i][0].Carrier_Doppler_hz;
                                    d_dump_file.write(reinterpret_cast<char *>(&tmp_double), sizeof(double));
                                    tmp_double = out[i][0].Carrier_phase_rads / GPS_TWO_PI;
                                    d_dump_file.write(reinterpret_cast<char *>(&tmp_double), sizeof(double));
                                    tmp_double = out[i][0].Pseudorange_m;
                                    d_dump_file.write(reinterpret_cast<char *>(&tmp_double), sizeof(double));
                                    tmp_double = static_cast<double>(out[i][0].PRN);
                                    d_dump_file.write(reinterpret_cast<char *>(&tmp_double), sizeof(double));
                                    tmp_double = static_cast<double>(out[i][0].Flag_valid_pseudorange);
                                    d_dump_file.write(reinterpret_cast<char *>(&tmp_double), sizeof(double));
                                }
                        }
                    catch (const std::ifstream::failure &e)
                        {
                            LOG(WARNING) << "Exception writing observables dump file " << e.what();
                            d_dump = false;
                        }
                }
            return 1;
        }
    return 0;
}
