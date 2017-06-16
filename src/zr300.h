// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2016 Intel Corporation. All Rights Reserved.

#pragma once
#ifndef LIBREALSENSE_ZR300_H
#define LIBREALSENSE_ZR300_H

#include "motion-module.h"
#include "motion-common.h"
#include "ds-device.h"
#include "sync.h"

#include <deque>
#include <cmath>
#include <math.h>

namespace rsimpl
{
   
    


    
   
    enum class auto_exposure_modes {
        static_auto_exposure = 0,
        auto_exposure_anti_flicker,
        auto_exposure_hybrid
    };

    class fisheye_auto_exposure_state
    {
    public:
        fisheye_auto_exposure_state() :
            is_auto_exposure(true),
            mode(auto_exposure_modes::auto_exposure_hybrid),
            rate(60),
            sample_rate(1),
            skip_frames(2)
        {}

        unsigned get_auto_exposure_state(rs_option option) const;
        void set_auto_exposure_state(rs_option option, double value);

    private:
        bool                is_auto_exposure;
        auto_exposure_modes mode;
        unsigned            rate;
        unsigned            sample_rate;
        unsigned            skip_frames;
    };

    class zr300_camera;
    class auto_exposure_algorithm {
    public:
        void modify_exposure(float& exposure_value, bool& exp_modified, float& gain_value, bool& gain_modified); // exposure_value in milliseconds
        bool analyze_image(const rs_frame_ref* image);
        auto_exposure_algorithm(fisheye_auto_exposure_state auto_exposure_state);
        void update_options(const fisheye_auto_exposure_state& options);

    private:
        struct histogram_metric { int under_exposure_count; int over_exposure_count; int shadow_limit; int highlight_limit; int lower_q; int upper_q; float main_mean; float main_std; };
        enum class rounding_mode_type { round, ceil, floor };

        inline void im_hist(const uint8_t* data, const int width, const int height, const int rowStep, int h[]);
        void increase_exposure_target(float mult, float& target_exposure);
        void decrease_exposure_target(float mult, float& target_exposure);
        void increase_exposure_gain(const float& target_exposure, const float& target_exposure0, float& exposure, float& gain);
        void decrease_exposure_gain(const float& target_exposure, const float& target_exposure0, float& exposure, float& gain);
        void static_increase_exposure_gain(const float& target_exposure, const float& target_exposure0, float& exposure, float& gain);
        void static_decrease_exposure_gain(const float& target_exposure, const float& target_exposure0, float& exposure, float& gain);
        void anti_flicker_increase_exposure_gain(const float& target_exposure, const float& target_exposure0, float& exposure, float& gain);
        void anti_flicker_decrease_exposure_gain(const float& target_exposure, const float& target_exposure0, float& exposure, float& gain);
        void hybrid_increase_exposure_gain(const float& target_exposure, const float& target_exposure0, float& exposure, float& gain);
        void hybrid_decrease_exposure_gain(const float& target_exposure, const float& target_exposure0, float& exposure, float& gain);

#if defined(_WINDOWS) || defined(WIN32) || defined(WIN64)
        inline float round(float x) { return std::round(x); }
#else
        inline float round(float x) { return x < 0.0 ? std::ceil(x - 0.5f) : std::floor(x + 0.5f); }
#endif

        float exposure_to_value(float exp_ms, rounding_mode_type rounding_mode);
        float gain_to_value(float gain, rounding_mode_type rounding_mode);
        template <typename T> inline T sqr(const T& x) { return (x*x); }
        void histogram_score(std::vector<int>& h, const int total_weight, histogram_metric& score);


        float minimal_exposure = 0.1f, maximal_exposure = 20.f, base_gain = 2.0f, gain_limit = 15.0f;
        float exposure = 10.0f, gain = 2.0f, target_exposure = 0.0f;
        uint8_t under_exposure_limit = 5, over_exposure_limit = 250; int under_exposure_noise_limit = 50, over_exposure_noise_limit = 50;
        int direction = 0, prev_direction = 0; float hysteresis = 0.075f;// 05;
        float eps = 0.01f, exposure_step = 0.1f, minimal_exposure_step = 0.01f;
        fisheye_auto_exposure_state state; float flicker_cycle; bool anti_flicker_mode = true;
        std::recursive_mutex state_mutex;
    };

    class auto_exposure_mechanism {
    public:
        auto_exposure_mechanism(zr300_camera* dev, fisheye_auto_exposure_state auto_exposure_state);
        ~auto_exposure_mechanism();
        void add_frame(rs_frame_ref* frame, std::shared_ptr<rsimpl::frame_archive> archive);
        void update_auto_exposure_state(fisheye_auto_exposure_state& auto_exposure_state);
        struct exposure_and_frame_counter {
            exposure_and_frame_counter() : exposure(0), frame_counter(0) {};
            exposure_and_frame_counter(double exposure, unsigned long long frame_counter) : exposure(exposure), frame_counter(frame_counter){}
            double exposure;
            unsigned long long frame_counter;
        };

    private:
        void push_back_data(rs_frame_ref* data);
        bool try_pop_front_data(rs_frame_ref** data);
        size_t get_queue_size();
        void clear_queue();
        unsigned get_skip_frames(const fisheye_auto_exposure_state& auto_exposure_state) { return auto_exposure_state.get_auto_exposure_state(RS_OPTION_FISHEYE_AUTO_EXPOSURE_SKIP_FRAMES); }
        void push_back_exp_and_cnt(exposure_and_frame_counter exp_and_cnt);
        bool try_get_exp_by_frame_cnt(double& exposure, const unsigned long long frame_counter);

        const std::size_t                      max_size_of_exp_and_cnt_queue = 10;
        zr300_camera*                          device;
        auto_exposure_algorithm                auto_exposure_algo;
        std::shared_ptr<rsimpl::frame_archive> sync_archive;
        std::shared_ptr<std::thread>           exposure_thread;
        std::condition_variable                cv;
        std::atomic<bool>                      keep_alive;
        std::deque<rs_frame_ref*>              data_queue;
        std::mutex                             queue_mtx;
        std::atomic<unsigned>                  frames_counter;
        std::atomic<unsigned>                  skip_frames;
        std::deque<exposure_and_frame_counter> exposure_and_frame_counter_queue;
        std::mutex                             exp_and_cnt_queue_mtx;
    };

    class zr300_camera final : public ds::ds_device
    {
        motion_module::motion_module_control     motion_module_ctrl;
        motion_module::mm_config                 motion_module_configuration;
        fisheye_auto_exposure_state              auto_exposure_state;
        std::shared_ptr<auto_exposure_mechanism> auto_exposure;
        std::atomic<bool>                        to_add_frames;

    protected:
        void toggle_motion_module_power(bool bOn);
        void toggle_motion_module_events(bool bOn);
        void on_before_callback(rs_stream , rs_frame_ref *, std::shared_ptr<rsimpl::frame_archive>) override;

        std::timed_mutex usbMutex;

    public:
        zr300_camera(std::shared_ptr<uvc::device> device, const static_device_info & info, motion_module_calibration fe_intrinsic, calibration_validator validator);
        ~zr300_camera();

        void get_option_range(rs_option option, double & min, double & max, double & step, double & def) override;
        void set_options(const rs_option options[], size_t count, const double values[]) override;
        void get_options(const rs_option options[], size_t count, double values[]) override;
        void send_blob_to_device(rs_blob_type type, void * data, int size) override;
        bool supports_option(rs_option option) const override;

        void start_motion_tracking() override;
        void stop_motion_tracking() override;

        void start(rs_source source) override;
        void stop(rs_source source) override;

        rs_stream select_key_stream(const std::vector<rsimpl::subdevice_mode_selection> & selected_modes) override;

        rs_motion_intrinsics get_motion_intrinsics() const override;
        rs_extrinsics get_motion_extrinsics_from(rs_stream from) const override;
        unsigned long long get_frame_counter_by_usb_cmd();

       
    private:
        unsigned get_auto_exposure_state(rs_option option);
        void set_auto_exposure_state(rs_option option, double value);
        void set_fw_logger_option(double value);
        unsigned get_fw_logger_option();

        bool validate_motion_extrinsics(rs_stream) const;
        bool validate_motion_intrinsics() const;

        motion_module_calibration fe_intrinsic;
    };
    motion_module_calibration read_fisheye_intrinsic(uvc::device & device);
    std::shared_ptr<rs_device> make_zr300_device(std::shared_ptr<uvc::device> device);
}

#endif
