/*!
 * \file gps_l5_dll_pll_tracking_fpga.h
 * \brief  Interface of an adapter of a DLL+PLL tracking loop block
 * for GPS L5 to a TrackingInterface for the FPGA
 * \author Marc Majoral, 2019. mmajoral(at)cttc.cat
 *         Javier Arribas, 2019. jarribas(at)cttc.es
 *
 * Code DLL + carrier PLL according to the algorithms described in:
 * K.Borre, D.M.Akos, N.Bertelsen, P.Rinder, and S.H.Jensen,
 * A Software-Defined GPS and Galileo Receiver. A Single-Frequency
 * Approach, Birkhauser, 2007
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
 * along with GNSS-SDR. If not, see <http://www.gnu.org/licenses/>.
 *
 * -------------------------------------------------------------------------
 */

#ifndef GNSS_SDR_GPS_L5_DLL_PLL_TRACKING_FPGA_H_
#define GNSS_SDR_GPS_L5_DLL_PLL_TRACKING_FPGA_H_

#include "dll_pll_veml_tracking_fpga.h"
#include "tracking_interface.h"
#include <gnuradio/runtime_types.h>
#include <cstddef>
#include <cstdint>
#include <string>

class Gnss_Synchro;
class ConfigurationInterface;

/*!
 * \brief This class implements a code DLL + carrier PLL tracking loop
 */
class GpsL5DllPllTrackingFpga : public TrackingInterface
{
public:
    GpsL5DllPllTrackingFpga(ConfigurationInterface* configuration,
        const std::string& role,
        unsigned int in_streams,
        unsigned int out_streams);

    virtual ~GpsL5DllPllTrackingFpga();

    inline std::string role() override
    {
        return role_;
    }

    //! Returns "GPS_L5_DLL_PLL_Tracking_Fpga"
    inline std::string implementation() override
    {
        return "GPS_L5_DLL_PLL_Tracking_Fpga";
    }

    inline size_t item_size() override
    {
        return sizeof(int);
    }

    void connect(gr::top_block_sptr top_block) override;
    void disconnect(gr::top_block_sptr top_block) override;
    gr::basic_block_sptr get_left_block() override;
    gr::basic_block_sptr get_right_block() override;

    /*!
     * \brief Set tracking channel unique ID
     */
    void set_channel(unsigned int channel) override;

    /*!
     * \brief Set acquisition/tracking common Gnss_Synchro object pointer
     * to efficiently exchange synchronization data between acquisition and tracking blocks
     */
    void set_gnss_synchro(Gnss_Synchro* p_gnss_synchro) override;

    void start_tracking() override;

    /*!
     * \brief Stop running tracking
     */
    void stop_tracking() override;

private:
    dll_pll_veml_tracking_fpga_sptr tracking_fpga_sc;
    uint32_t channel_;
    std::string role_;
    uint32_t in_streams_;
    uint32_t out_streams_;
    bool d_track_pilot;
    int32_t* d_ca_codes;
    int32_t* d_data_codes;
};

#endif  // GNSS_SDR_GPS_L5_DLL_PLL_TRACKING_FPGA_H_
