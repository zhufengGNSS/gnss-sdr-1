/*!
 * \file gps_l2c_telemetry_decoder_gs.cc
 * \brief Implementation of a NAV message demodulator block based on
 * Kay Borre book MATLAB-based GPS receiver
 * \author Javier Arribas, 2015. jarribas(at)cttc.es
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


#include "gps_l2c_telemetry_decoder_gs.h"
#include "GPS_L2C.h"  // for GPS_L2_CNAV_DATA_PAGE_BITS, GPS_L...
#include "display.h"
#include "gnss_synchro.h"
#include "gps_cnav_ephemeris.h"  // for Gps_CNAV_Ephemeris
#include "gps_cnav_iono.h"       // for Gps_CNAV_Iono
#include "gps_cnav_utc_model.h"  // for Gps_CNAV_Utc_Model
#include <glog/logging.h>
#include <gnuradio/io_signature.h>
#include <pmt/pmt.h>        // for make_any
#include <pmt/pmt_sugar.h>  // for mp
#include <bitset>           // for bitset
#include <cmath>            // for round
#include <exception>        // for exception
#include <iostream>         // for cout
#include <memory>           // for shared_ptr, make_shared


gps_l2c_telemetry_decoder_gs_sptr
gps_l2c_make_telemetry_decoder_gs(const Gnss_Satellite &satellite, bool dump)
{
    return gps_l2c_telemetry_decoder_gs_sptr(new gps_l2c_telemetry_decoder_gs(satellite, dump));
}


gps_l2c_telemetry_decoder_gs::gps_l2c_telemetry_decoder_gs(
    const Gnss_Satellite &satellite, bool dump) : gr::block("gps_l2c_telemetry_decoder_gs",
                                                      gr::io_signature::make(1, 1, sizeof(Gnss_Synchro)),
                                                      gr::io_signature::make(1, 1, sizeof(Gnss_Synchro)))
{
    // Ephemeris data port out
    this->message_port_register_out(pmt::mp("telemetry"));
    // Control messages to tracking block
    this->message_port_register_out(pmt::mp("telemetry_to_trk"));
    d_last_valid_preamble = 0;
    d_sent_tlm_failed_msg = false;
    d_max_symbols_without_valid_frame = GPS_L2_CNAV_DATA_PAGE_BITS * GPS_L2_SYMBOLS_PER_BIT * 5;  //rise alarm if 5 consecutive subframes have no valid CRC


    // initialize internal vars
    d_dump = dump;
    d_satellite = Gnss_Satellite(satellite.get_system(), satellite.get_PRN());
    DLOG(INFO) << "GPS L2C M TELEMETRY PROCESSING: satellite " << d_satellite;
    // set_output_multiple (1);
    d_channel = 0;
    d_flag_valid_word = false;
    d_TOW_at_current_symbol = 0;
    d_TOW_at_Preamble = 0;
    d_state = 0;  //initial state
    d_crc_error_count = 0;

    // initialize the CNAV frame decoder (libswiftcnav)
    cnav_msg_decoder_init(&d_cnav_decoder);

    d_sample_counter = 0;
}


gps_l2c_telemetry_decoder_gs::~gps_l2c_telemetry_decoder_gs()
{
    if (d_dump_file.is_open() == true)
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
}


void gps_l2c_telemetry_decoder_gs::set_satellite(const Gnss_Satellite &satellite)
{
    d_satellite = Gnss_Satellite(satellite.get_system(), satellite.get_PRN());
    DLOG(INFO) << "GPS L2C CNAV telemetry decoder in channel " << this->d_channel << " set to satellite " << d_satellite;
}


void gps_l2c_telemetry_decoder_gs::set_channel(int channel)
{
    d_channel = channel;
    LOG(INFO) << "GPS L2C CNAV channel set to " << channel;
    // ############# ENABLE DATA FILE LOG #################
    if (d_dump == true)
        {
            if (d_dump_file.is_open() == false)
                {
                    try
                        {
                            d_dump_filename = "telemetry_L2CM_";
                            d_dump_filename.append(std::to_string(d_channel));
                            d_dump_filename.append(".dat");
                            d_dump_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
                            d_dump_file.open(d_dump_filename.c_str(), std::ios::out | std::ios::binary);
                            LOG(INFO) << "Telemetry decoder dump enabled on channel " << d_channel
                                      << " Log file: " << d_dump_filename.c_str();
                        }
                    catch (const std::ifstream::failure &e)
                        {
                            LOG(WARNING) << "channel " << d_channel << " Exception opening Telemetry GPS L2 dump file " << e.what();
                        }
                }
        }
}


void gps_l2c_telemetry_decoder_gs::reset()
{
    d_last_valid_preamble = d_sample_counter;
    d_sent_tlm_failed_msg = false;
    DLOG(INFO) << "Telemetry decoder reset for satellite " << d_satellite;
}


int gps_l2c_telemetry_decoder_gs::general_work(int noutput_items __attribute__((unused)), gr_vector_int &ninput_items __attribute__((unused)),
    gr_vector_const_void_star &input_items, gr_vector_void_star &output_items)
{
    // get pointers on in- and output gnss-synchro objects
    auto *out = reinterpret_cast<Gnss_Synchro *>(output_items[0]);            // Get the output buffer pointer
    const auto *in = reinterpret_cast<const Gnss_Synchro *>(input_items[0]);  // Get the input buffer pointer

    bool flag_new_cnav_frame = false;
    cnav_msg_t msg;
    uint32_t delay = 0;

    // add the symbol to the decoder
    uint8_t symbol_clip = static_cast<uint8_t>(in[0].Prompt_I > 0) * 255;
    flag_new_cnav_frame = cnav_msg_decoder_add_symbol(&d_cnav_decoder, symbol_clip, &msg, &delay);

    consume_each(1);  // one by one

    // check if there is a problem with the telemetry of the current satellite
    d_sample_counter++;  // count for the processed symbols
    if (d_sent_tlm_failed_msg == false)
        {
            if ((d_sample_counter - d_last_valid_preamble) > d_max_symbols_without_valid_frame)
                {
                    int message = 1;  //bad telemetry
                    this->message_port_pub(pmt::mp("telemetry_to_trk"), pmt::make_any(message));
                    d_sent_tlm_failed_msg = true;
                }
        }

    // UPDATE GNSS SYNCHRO DATA
    Gnss_Synchro current_synchro_data{};  // structure to save the synchronization information and send the output object to the next block

    // 1. Copy the current tracking output
    current_synchro_data = in[0];

    // 2. Add the telemetry decoder information
    // check if new CNAV frame is available
    if (flag_new_cnav_frame == true)
        {
            std::bitset<GPS_L2_CNAV_DATA_PAGE_BITS> raw_bits;
            // Expand packet bits to bitsets. Notice the reverse order of the bits sequence, required by the CNAV message decoder
            for (uint32_t i = 0; i < GPS_L2_CNAV_DATA_PAGE_BITS; i++)
                {
                    raw_bits[GPS_L2_CNAV_DATA_PAGE_BITS - 1 - i] = ((msg.raw_msg[i / 8] >> (7 - i % 8)) & 1u);
                }

            d_CNAV_Message.decode_page(raw_bits);

            // Push the new navigation data to the queues
            if (d_CNAV_Message.have_new_ephemeris() == true)
                {
                    // get ephemeris object for this SV
                    std::shared_ptr<Gps_CNAV_Ephemeris> tmp_obj = std::make_shared<Gps_CNAV_Ephemeris>(d_CNAV_Message.get_ephemeris());
                    std::cout << TEXT_BLUE << "New GPS CNAV message received in channel " << d_channel << ": ephemeris from satellite " << d_satellite << TEXT_RESET << std::endl;
                    this->message_port_pub(pmt::mp("telemetry"), pmt::make_any(tmp_obj));
                }
            if (d_CNAV_Message.have_new_iono() == true)
                {
                    std::shared_ptr<Gps_CNAV_Iono> tmp_obj = std::make_shared<Gps_CNAV_Iono>(d_CNAV_Message.get_iono());
                    std::cout << TEXT_BLUE << "New GPS CNAV message received in channel " << d_channel << ": iono model parameters from satellite " << d_satellite << TEXT_RESET << std::endl;
                    this->message_port_pub(pmt::mp("telemetry"), pmt::make_any(tmp_obj));
                }

            if (d_CNAV_Message.have_new_utc_model() == true)
                {
                    std::shared_ptr<Gps_CNAV_Utc_Model> tmp_obj = std::make_shared<Gps_CNAV_Utc_Model>(d_CNAV_Message.get_utc_model());
                    std::cout << TEXT_BLUE << "New GPS CNAV message received in channel " << d_channel << ": UTC model parameters from satellite " << d_satellite << TEXT_RESET << std::endl;
                    this->message_port_pub(pmt::mp("telemetry"), pmt::make_any(tmp_obj));
                }

            // update TOW at the preamble instant
            d_TOW_at_Preamble = static_cast<double>(msg.tow);
            d_last_valid_preamble = d_sample_counter;
            // The time of the last input symbol can be computed from the message ToW and
            // delay by the formulae:
            // \code
            // symbolTime_ms = msg->tow * 6000 + *pdelay * 20 + (12 * 20); 12 symbols of the encoder's transitory
            d_TOW_at_current_symbol = static_cast<double>(msg.tow) * 6.0 + static_cast<double>(delay) * GPS_L2_M_PERIOD + 12 * GPS_L2_M_PERIOD;
            //d_TOW_at_current_symbol = floor(d_TOW_at_current_symbol * 1000.0) / 1000.0;
            d_flag_valid_word = true;
        }
    else
        {
            d_TOW_at_current_symbol += GPS_L2_M_PERIOD;
            if (current_synchro_data.Flag_valid_symbol_output == false)
                {
                    d_flag_valid_word = false;
                }
        }
    current_synchro_data.TOW_at_current_symbol_ms = round(d_TOW_at_current_symbol * 1000.0);
    current_synchro_data.Flag_valid_word = d_flag_valid_word;

    if (d_dump == true)
        {
            // MULTIPLEXED FILE RECORDING - Record results to file
            try
                {
                    double tmp_double;
                    uint64_t tmp_ulong_int;
                    tmp_double = d_TOW_at_current_symbol;
                    d_dump_file.write(reinterpret_cast<char *>(&tmp_double), sizeof(double));
                    tmp_ulong_int = current_synchro_data.Tracking_sample_counter;
                    d_dump_file.write(reinterpret_cast<char *>(&tmp_ulong_int), sizeof(uint64_t));
                    tmp_double = d_TOW_at_Preamble;
                    d_dump_file.write(reinterpret_cast<char *>(&tmp_double), sizeof(double));
                }
            catch (const std::ifstream::failure &e)
                {
                    LOG(WARNING) << "Exception writing Telemetry GPS L2 dump file " << e.what();
                }
        }

    // 3. Make the output (copy the object contents to the GNURadio reserved memory)
    out[0] = current_synchro_data;
    return 1;
}
