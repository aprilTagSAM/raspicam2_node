#include <RasPiCamPublisherNode.hpp>

RasPiCamPublisher::RasPiCamPublisher() : Node("raspicam2", "camera", true) {
    state = std::make_shared<RASPIVID_STATE>();

    configure_parameters(*state);

    // get parameters
    int w, h, f, q;
    get_parameter_or("width", w, 320);
    get_parameter_or("height", h, 240);
    get_parameter_or("fps", f, 90);
    get_parameter_or("quality", q, 80);

    // std::cout << "width: " << w << std::endl;
    // std::cout << "height: " << h << std::endl;
    // std::cout << "fps: " << w << std::endl;
    // std::cout << "quality: " << q << std::endl;

    // set default camera parameters for Camera Module v1
    // https://www.raspberrypi.org/documentation/hardware/camera/
    // camera centre: 3.76 × 2.74 mm
    // focal length: 3.60 mm +/- 0.01
    const double fx = (3.60 / 3.76) * w;
    const double fy = (3.60 / 2.74) * h;
    const double cx = w/2.0;
    const double cy = h/2.0;
    camera_info.width = w;
    camera_info.height = h;
    camera_info.k = {fx, 0,  cx,
                     0,  fy, cy,
                     0,   0, 1};

    state->width = w;
    state->height = h;
    state->quality = q;
    state->framerate = f;

    std::string image_transport;
    get_parameter_or<std::string>("image_transport", image_transport, "raw");
    state->enable_raw_pub = (image_transport=="raw");
    get_parameter_or("enable_imv", state->enable_imv_pub, false);

    buffer_callback_t cb_raw = nullptr;
    pub_img = nullptr;
    if(state->enable_raw_pub) {
        pub_img = create_publisher<sensor_msgs::msg::Image>("image");
        cb_raw = std::bind(&RasPiCamPublisher::onImageRaw, this, std::placeholders::_1, std::placeholders::_2);
    }

    buffer_callback_t cb_motion = nullptr;
    if(state->enable_imv_pub) {
        cb_motion = std::bind(&RasPiCamPublisher::onMotion, this, std::placeholders::_1, std::placeholders::_2);
    }


    get_parameter_or("camera_id", state->camera_id, 0);

    get_parameter_or("sharpness", state->camera_parameters.sharpness, 0);
    get_parameter_or("contrast", state->camera_parameters.contrast, 0);
    get_parameter_or("brightness", state->camera_parameters.brightness, 0);
    get_parameter_or("saturation", state->camera_parameters.saturation, 0);
    get_parameter_or("ISO", state->camera_parameters.ISO, 0);
    get_parameter_or("videoStabilisation", state->camera_parameters.videoStabilisation, 0);
    get_parameter_or("exposureCompensation", state->camera_parameters.exposureCompensation, 0);
    // get_parameter_or("exposureMode", state->camera_parameters.exposureMode, AUTO);
    // get_parameter_or("flickerAvoidMode", state->camera_parameters.flickerAvoidMode, OFF);
    // get_parameter_or("exposureMeterMode", state->camera_parameters.exposureMeterMode, AVERAGE);
    // get_parameter_or("awbMode", state->camera_parameters.awbMode, AUTO);
    // get_parameter_or("imageEffect", state->camera_parameters.imageEffect, NONE);
    get_parameter_or("colourEffects_enable", state->camera_parameters.colourEffects.enable, 0);
    get_parameter_or("colourEffects_u", state->camera_parameters.colourEffects.u, 128);
    get_parameter_or("colourEffects_v", state->camera_parameters.colourEffects.v, 128);
    get_parameter_or("rotation", state->camera_parameters.rotation, 0);
    get_parameter_or("hflip", state->camera_parameters.hflip, 0);
    get_parameter_or("vflip", state->camera_parameters.vflip, 0);
    get_parameter_or("roi_x", state->camera_parameters.roi.x, 0.0);
    get_parameter_or("roi_y", state->camera_parameters.roi.y, 0.0);
    get_parameter_or("roi_w", state->camera_parameters.roi.w, 0.0);
    get_parameter_or("roi_h", state->camera_parameters.roi.h, 1.0);
    get_parameter_or("shutter_speed", state->camera_parameters.shutter_speed, 0);

    get_parameter_or("awb_gains_r", state->camera_parameters.awb_gains_r, 0.0f);
    get_parameter_or("awb_gains_b", state->camera_parameters.awb_gains_b, 0.0f);

    get_parameter_or("analog_gain", state->camera_parameters.awb_gains_r, 0.0f);
    get_parameter_or("digital_gain", state->camera_parameters.awb_gains_b, 0.0f);

    pub_img_compressed = create_publisher<sensor_msgs::msg::CompressedImage>("image/compressed");
    pub_info = create_publisher<sensor_msgs::msg::CameraInfo>("image/camera_info");
    srv_info = create_service<sensor_msgs::srv::SetCameraInfo>("set_camera_info",
        std::bind(&RasPiCamPublisher::set_camera_info, this,
        std::placeholders::_1, std::placeholders::_2));

    init_cam(*state,
             cb_raw,
             std::bind(&RasPiCamPublisher::onImageCompressed, this, std::placeholders::_1, std::placeholders::_2),
             cb_motion);

    start_capture(*state);
}

RasPiCamPublisher::~RasPiCamPublisher() {
    close_cam(*state);
}

void RasPiCamPublisher::onImageRaw(const uint8_t* start, const uint8_t* end) {

    const auto tnow = now();

    sensor_msgs::msg::Image::UniquePtr msg(new sensor_msgs::msg::Image());
    msg->header.frame_id = "camera";
    msg->header.stamp = tnow;
    msg->encoding = "rgb8";
    msg->data.insert(msg->data.end(), start, end);
    pub_img->publish(msg);

    camera_info.header.frame_id = "camera";
    camera_info.header.stamp = tnow;
    pub_info->publish(camera_info);
}

void RasPiCamPublisher::onImageCompressed(const uint8_t* start, const uint8_t* end) {

    const auto tnow = now();

    sensor_msgs::msg::CompressedImage::UniquePtr msg(new sensor_msgs::msg::CompressedImage());
    msg->header.frame_id = "camera";
    msg->header.stamp = tnow;
    // set raw compressed data
    msg->format = "jpeg";
    msg->data.insert(msg->data.end(), start, end);
    pub_img_compressed->publish(msg);

    camera_info.header.frame_id = "camera";
    camera_info.header.stamp = tnow;
    pub_info->publish(camera_info);
}

void RasPiCamPublisher::onMotion(const uint8_t* start, const uint8_t* end) { }

void RasPiCamPublisher::set_camera_info(
    const std::shared_ptr<sensor_msgs::srv::SetCameraInfo::Request> req,
    std::shared_ptr<sensor_msgs::srv::SetCameraInfo::Response> res)
{
    camera_info = req->camera_info;
    res->success = true;
}

#include <class_loader/register_macro.hpp>
CLASS_LOADER_REGISTER_CLASS(RasPiCamPublisher, rclcpp::Node)
