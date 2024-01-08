// https://gist.github.com/kervel/75d81b8c34e45a19706c661b90d02548
#include "depthai_ros_py_bindings/bindings.hpp"

#include <pybind11/complex.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include "ament_index_cpp/get_resource.hpp"
#include "class_loader/class_loader.hpp"
#include "depthai_bridge/ImageConverter.hpp"
#include "geometry_msgs/msg/point.hpp"
#include "pybind11/pybind11.h"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/node_factory.hpp"
#include "rcpputils/filesystem_helper.hpp"
#include "rcpputils/split.hpp"

namespace dai {
namespace ros {
namespace py = pybind11;

ImgStreamer::ImgStreamer(rclcpp::Node::SharedPtr node,
                         dai::CalibrationHandler calibHandler,
                         dai::CameraBoardSocket socket,
                         const std::string& topicName,
                         const std::string& frameName,
                         int width,
                         int height,
                         bool interleaved,
                         bool getBaseDeviceTimestamp)
    : _imageConverter(std::make_shared<ImageConverter>(frameName, interleaved, getBaseDeviceTimestamp)),
      _publishCompressed(false),
      _ipcEnabled(node->get_node_options().use_intra_process_comms()) {
    _imageConverter->setUpdateRosBaseTimeOnToRosMsg(true);

    RCLCPP_INFO(node->get_logger(), "Creating publisher for '%s'", topicName.c_str());

    if(_ipcEnabled) {
        _callbackGroup = node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
        rclcpp::PublisherOptions opts;
        opts.callback_group = _callbackGroup;
        _pub = node->create_publisher<sensor_msgs::msg::Image>(topicName, 10);
        _pubCompressed = node->create_publisher<sensor_msgs::msg::CompressedImage>(topicName + "/compressed", 10);
        _pubCamInfo = node->create_publisher<sensor_msgs::msg::CameraInfo>(topicName + "/camera_info", 10);
    } else {
        _pubCamera = image_transport::create_camera_publisher(node.get(), topicName);
    }
    _camInfoMsg = _imageConverter->calibrationToCameraInfo(calibHandler, socket, width, height);
}
void ImgStreamer::publish(const std::string& name, std::shared_ptr<dai::ImgFrame> imgFrame) {
    auto imgMsg = _imageConverter->toRosMsgRawPtr(imgFrame);
    _camInfoMsg.header = imgMsg.header;
    if(_ipcEnabled) {
        if(_publishCompressed) {
            sensor_msgs::msg::CompressedImage compressedImgMsg;
            compressedImgMsg.header = imgMsg.header;
            compressedImgMsg.format = "jpeg";
            size_t size = imgFrame->getData().size();
            compressedImgMsg.data.resize(size);
            compressedImgMsg.data.assign(imgFrame->getData().begin(), imgFrame->getData().end());
            _pubCompressed->publish(compressedImgMsg);
        }
        _pub->publish(imgMsg);
        _pubCamInfo->publish(_camInfoMsg);
    } else {
        _pubCamera.publish(imgMsg, _camInfoMsg);
    }
}

void ImgStreamer::convertFromBitstream(dai::RawImgFrame::Type type) {
    _imageConverter->convertFromBitstream(type);
    _publishCompressed = true;
}

ImuStreamer::ImuStreamer(rclcpp::Node::SharedPtr node,
                         const std::string& topicName,
                         const std::string& frameName,
                         ImuSyncMethod syncMode,
                         double linear_accel_cov,
                         double angular_velocity_cov,
                         double rotation_cov,
                         double magnetic_field_cov,
                         bool enable_rotation,
                         bool enable_magn,
                         bool getBaseDeviceTimestamp) {
    _imuConverter = std::make_shared<ImuConverter>(
        frameName, syncMode, linear_accel_cov, angular_velocity_cov, rotation_cov, magnetic_field_cov, enable_rotation, enable_magn, getBaseDeviceTimestamp);
    _imuConverter->setUpdateRosBaseTimeOnToRosMsg(true);
    _pub = node->create_publisher<sensor_msgs::msg::Imu>(topicName, 10);
}

void ImuStreamer::publish(const std::string& name, std::shared_ptr<dai::IMUData> imuFrame) {
    std::deque<sensor_msgs::msg::Imu> imuMsgs;
    _imuConverter->toRosMsg(imuFrame, imuMsgs);
    for(auto& msg : imuMsgs) {
        _pub->publish(msg);
    };
}

SpatialDetectionStreamer::SpatialDetectionStreamer(
    rclcpp::Node::SharedPtr node, const std::string& topicName, std::string frameName, int width, int height, bool normalized, bool getBaseDeviceTimestamp) {
    _spatialDetectionConverter = std::make_shared<SpatialDetectionConverter>(frameName, width, height, normalized, getBaseDeviceTimestamp);
    _pub = node->create_publisher<vision_msgs::msg::Detection3DArray>(topicName, 10);
}

void SpatialDetectionStreamer::publish(const std::string& name, std::shared_ptr<dai::SpatialImgDetections> detections) {
    std::deque<vision_msgs::msg::Detection3DArray> detectionMsg;
    _spatialDetectionConverter->toRosVisionMsg(detections, detectionMsg);
    for(auto& msg : detectionMsg) {
        _pub->publish(msg);
    };
}

DetectionStreamer::DetectionStreamer(
    rclcpp::Node::SharedPtr node, const std::string& topicName, std::string frameName, int width, int height, bool normalized, bool getBaseDeviceTimestamp) {
    _detectionConverter = std::make_shared<ImgDetectionConverter>(frameName, width, height, normalized, getBaseDeviceTimestamp);
    _pub = node->create_publisher<vision_msgs::msg::Detection2DArray>(topicName, 10);
}

void DetectionStreamer::publish(const std::string& name, std::shared_ptr<dai::ImgDetections> detections) {
    std::deque<vision_msgs::msg::Detection2DArray> detectionMsg;
    _detectionConverter->toRosMsg(detections, detectionMsg);
    for(auto& msg : detectionMsg) {
        _pub->publish(msg);
    };
}

TrackedFeaturesStreamer::TrackedFeaturesStreamer(rclcpp::Node::SharedPtr node,
                                                 const std::string& topicName,
                                                 std::string frameName,
                                                 bool getBaseDeviceTimestamp) {
    _trackedFeaturesConverter = std::make_shared<TrackedFeaturesConverter>(frameName, getBaseDeviceTimestamp);
    _pub = node->create_publisher<depthai_ros_msgs::msg::TrackedFeatures>(topicName, 10);
}

void TrackedFeaturesStreamer::publish(const std::string& name, std::shared_ptr<dai::TrackedFeatures> trackedFeatures) {
    std::deque<depthai_ros_msgs::msg::TrackedFeatures> trackedFeaturesMsg;
    _trackedFeaturesConverter->toRosMsg(trackedFeatures, trackedFeaturesMsg);
    for(auto& msg : trackedFeaturesMsg) {
        _pub->publish(msg);
    };
}

void ROSContextManager::init(py::list args, const std::string& executorType) {
    _executorType = executorType;
    std::vector<const char*> c_strs;
    for(auto& a : args) {
        c_strs.push_back(a.cast<std::string>().c_str());
    }
    rclcpp::init(c_strs.size(), c_strs.data());
    if(executorType == "single_threaded")
        _singleExecutor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    else if(executorType == "multi_threaded")
        _multiExecutor = std::make_shared<rclcpp::executors::MultiThreadedExecutor>();
}

void ROSContextManager::shutdown() {
    if(_executorType == "single_threaded") {
        _singleExecutor->cancel();
    } else if(_executorType == "multi_threaded") {
        _multiExecutor->cancel();
    } else
        throw std::runtime_error("Unknown executor type");
}

void ROSContextManager::addNode(rclcpp::Node::SharedPtr node) {
    if(_executorType == "single_threaded")
        _singleExecutor->add_node(node);
    else if(_executorType == "multi_threaded")
        _multiExecutor->add_node(node);
    else {
        throw std::runtime_error("Unknown executor type");
    }
}

void ROSContextManager::addComposableNode(const std::string& packageName, const std::string& pluginName, const rclcpp::NodeOptions& options) {
    std::string content;
    std::string basePath;
    auto resourcePath = ament_index_cpp::get_resource("rclcpp_components", packageName, content, &basePath);
    std::string libraryPath;
    std::vector<std::string> lines = rcpputils::split(content, '\n', true);
    for(const auto& line : lines) {
        std::vector<std::string> parts = rcpputils::split(line, ';');
        if(parts.size() != 2) {
            throw std::runtime_error("Invalid resource file");
        }
        libraryPath = parts[1];
        if(!rcpputils::fs::path(libraryPath).is_absolute()) {
            libraryPath = basePath + "/" + libraryPath;
        }
        if(parts[0] == pluginName) {
            break;
        }
    }
    RCLCPP_INFO(rclcpp::get_logger("dai_ros_py"), "Loading library '%s'", libraryPath.c_str());
    std::shared_ptr<class_loader::ClassLoader> libLoader;
    try {
        libLoader = std::make_shared<class_loader::ClassLoader>(libraryPath);
    } catch(const std::exception& e) {
        RCLCPP_ERROR(rclcpp::get_logger("dai_ros_py"), "Failed to load library '%s'. Reason: %s", libraryPath.c_str(), e.what());
        return;
    }

    std::shared_ptr<rclcpp_components::NodeFactory> nodeFactory;
    auto classes = libLoader->getAvailableClasses<rclcpp_components::NodeFactory>();

    std::string fqPluginName = "rclcpp_components::NodeFactoryTemplate<" + pluginName + ">";
    bool foundClass = false;
    for(const auto& clazz : classes) {
        RCLCPP_INFO(rclcpp::get_logger("dai_ros_py"), "Found class: %s", clazz.c_str());
        if(clazz == pluginName || clazz == fqPluginName) {
            nodeFactory = libLoader->createInstance<rclcpp_components::NodeFactory>(clazz);
            foundClass = true;
            RCLCPP_INFO(rclcpp::get_logger("dai_ros_py"), "Loaded class '%s' from library '%s'", pluginName.c_str(), libraryPath.c_str());
            break;
        }
    }
    if(!foundClass) {
        RCLCPP_ERROR(rclcpp::get_logger("dai_ros_py"), "Failed to find class '%s' in library '%s'", pluginName.c_str(), libraryPath.c_str());
        return;
    }
    auto node = nodeFactory->create_node_instance(options);
    if(_executorType == "single_threaded")
        _singleExecutor->add_node(node.get_node_base_interface());
    else if(_executorType == "multi_threaded")
        _multiExecutor->add_node(node.get_node_base_interface());
    else {
        throw std::runtime_error("Unknown executor type");
    }
    _composableNodes.push_back(node);
}

void ROSContextManager::spin() {
    if(_executorType == "single_threaded") {
        auto spin_node = [this]() { _singleExecutor->spin(); };
        _executionThread = std::thread(spin_node);
    } else if(_executorType == "multi_threaded") {
        auto spin_node = [this]() { _multiExecutor->spin(); };
        _executionThread = std::thread(spin_node);
    } else
        throw std::runtime_error("Unknown executor type");
    _executionThread.detach();
}

PYBIND11_MODULE(dai_ros_py, m) {
    m.doc() = "depthai-ros bindings";
    auto point = message_class<geometry_msgs::msg::Point>(m, "Point");
    point.def(py::init<>());
    point.def_readwrite("x", &geometry_msgs::msg::Point::x);
    point.def_readwrite("y", &geometry_msgs::msg::Point::y);
    point.def_readwrite("z", &geometry_msgs::msg::Point::z);

    py::class_<rclcpp::Node, rclcpp::Node::SharedPtr> node(m, "ROSNode");
    node.def(py::init(
        [](std::string nodename, const rclcpp::NodeOptions& options = rclcpp::NodeOptions()) { return std::make_shared<rclcpp::Node>(nodename, options); }));

    py::class_<rclcpp::NodeOptions> nodeOptions(m, "ROSNodeOptions");
    nodeOptions.def(py::init([](std::string nodeName = "",
                                std::string ns = "",
                                std::string paramFile = "",
                                remappingsMap remappings = remappingsMap(),
                                bool useIntraProcessComms = false) {
                        rclcpp::NodeOptions options;
                        options.use_intra_process_comms(useIntraProcessComms);
                        std::vector<std::string> args;
                        if(!paramFile.empty()) {
                            args.push_back("--ros-args");
                            args.push_back("--params-file");
                            args.push_back(paramFile);
                        }

                        if(!nodeName.empty()) {
                            args.push_back("--ros-args");
                            args.push_back("--remap");
                            args.push_back("__node:=" + nodeName);
                        }
                        if(!ns.empty()) {
                            args.push_back("--ros-args");
                            args.push_back("--remap");
                            args.push_back("__ns:=" + ns);
                        }
                        if(!remappings.empty()) {
                            for(auto& remap : remappings) {
                                args.push_back("--remap");
                                args.push_back(remap.first + ":=" + remap.second);
                            }
                        }

                        options.arguments(args);
                        return options;
                    }),
                    py::arg("node_name") = "",
                    py::arg("ns") = "",
                    py::arg("param_file") = "",
                    py::arg("remappings") = remappingsMap(),
                    py::arg("use_intra_process_comms") = false);

    py::class_<Consumer, std::shared_ptr<Consumer>, rclcpp::Node> consumer(m, "Consumer");
    consumer.def(py::init(
        [](std::string nodename, const rclcpp::NodeOptions& options, std::string input) { return std::make_shared<Consumer>(nodename, options, input); }));
    py::class_<Producer, std::shared_ptr<Producer>, rclcpp::Node> producer(m, "Producer");
    producer.def(py::init(
        [](std::string nodename, const rclcpp::NodeOptions& options, std::string output) { return std::make_shared<Producer>(nodename, options, output); }));

    py::class_<ROSContextManager, std::shared_ptr<ROSContextManager>> rosContextManager(m, "ROSContextManager");
    rosContextManager.def(py::init([]() { return std::make_shared<ROSContextManager>(); }));
    rosContextManager.def("add_node", &ROSContextManager::addNode);
    rosContextManager.def("add_composable_node", &ROSContextManager::addComposableNode);
    rosContextManager.def("init", &ROSContextManager::init);
    rosContextManager.def("shutdown", &ROSContextManager::shutdown);
    rosContextManager.def("spin", &ROSContextManager::spin);

    node.def("create_subscription", [](rclcpp::Node::SharedPtr n, py::object the_type, std::string topic, py::function callback) {
        auto f = the_type.attr("__create_subscription__");
        return f(n, topic, callback);
    });
    node.def("create_publisher", [](rclcpp::Node::SharedPtr n, py::object the_type, std::string topic) {
        auto f = the_type.attr("__create_publisher__");
        return f(n, topic);
    });

    node.def("log", [](rclcpp::Node::SharedPtr n, std::string logmsg) { RCLCPP_INFO(n->get_logger(), logmsg.c_str()); });

    m.def("ros_ok", []() { return rclcpp::ok(); });

    m.def("shutdown", []() { rclcpp::shutdown(); });

    py::class_<ImgStreamer, std::shared_ptr<ImgStreamer>> imgStreamer(m, "ImgStreamer");

    imgStreamer.def(py::init([](rclcpp::Node::SharedPtr node,
                                dai::CalibrationHandler calibHandler,
                                dai::CameraBoardSocket socket,
                                const std::string& topicName,
                                const std::string& frameName,
                                int width,
                                int height,
                                bool interleaved,
                                bool getBaseDeviceTimestamp) {
                        return std::make_shared<ImgStreamer>(
                            node, calibHandler, socket, topicName, frameName, width, height, interleaved, getBaseDeviceTimestamp);
                    }),
                    py::arg("node"),
                    py::arg("calib_handler"),
                    py::arg("socket"),
                    py::arg("topic_name"),
                    py::arg("frame_name"),
                    py::arg("width") = -1,
                    py::arg("height") = -1,
                    py::arg("interleaved") = false,
                    py::arg("get_base_device_timestamp") = false);

    imgStreamer.def("publish", &ImgStreamer::publish);
    imgStreamer.def("convertFromBitStream", &ImgStreamer::convertFromBitstream);

    py::enum_<ImuSyncMethod>(m, "ImuSyncMethod")
        .value("COPY", ImuSyncMethod::COPY)
        .value("LINEAR_INTERPOLATE_ACCEL", ImuSyncMethod::LINEAR_INTERPOLATE_ACCEL)
        .value("LINEAR_INTERPOLATE_GYRO", ImuSyncMethod::LINEAR_INTERPOLATE_GYRO)
        .export_values();

    py::class_<ImuStreamer, std::shared_ptr<ImuStreamer>> imuStreamer(m, "ImuStreamer");
    imuStreamer.def(py::init([](rclcpp::Node::SharedPtr node,
                                const std::string& topicName,
                                const std::string& frameName,
                                ImuSyncMethod syncMode,
                                double linear_accel_cov,
                                double angular_velocity_cov,
                                double rotation_cov,
                                double magnetic_field_cov,
                                bool enable_rotation,
                                bool enable_magn,
                                bool getBaseDeviceTimestamp) {
                        return std::make_shared<ImuStreamer>(node,
                                                             topicName,
                                                             frameName,
                                                             syncMode,
                                                             linear_accel_cov,
                                                             angular_velocity_cov,
                                                             rotation_cov,
                                                             magnetic_field_cov,
                                                             enable_rotation,
                                                             enable_magn,
                                                             getBaseDeviceTimestamp);
                    }),
                    py::arg("node"),
                    py::arg("topic_name"),
                    py::arg("frame_name"),
                    py::arg("sync_mode") = ImuSyncMethod::LINEAR_INTERPOLATE_ACCEL,
                    py::arg("linear_accel_cov") = 0.0,
                    py::arg("angular_velocity_cov") = 0.0,
                    py::arg("rotation_cov") = 0.0,
                    py::arg("magnetic_field_cov") = 0.0,
                    py::arg("enable_rotation") = false,
                    py::arg("enable_magn") = false,
                    py::arg("get_base_device_timestamp") = false);
    imuStreamer.def("publish", &ImuStreamer::publish);

    py::class_<SpatialDetectionStreamer, std::shared_ptr<SpatialDetectionStreamer>> spatialDetectionStreamer(m, "SpatialDetectionStreamer");
    spatialDetectionStreamer.def(py::init([](rclcpp::Node::SharedPtr node,
                                             const std::string& topicName,
                                             std::string frameName,
                                             int width,
                                             int height,
                                             bool normalized = false,
                                             bool getBaseDeviceTimestamp = false) {
                                     return std::make_shared<SpatialDetectionStreamer>(
                                         node, topicName, frameName, width, height, normalized, getBaseDeviceTimestamp);
                                 }),
                                 py::arg("node"),
                                 py::arg("topic_name"),
                                 py::arg("frame_name"),
                                 py::arg("width"),
                                 py::arg("height"),
                                 py::arg("normalized") = false,
                                 py::arg("get_base_device_timestamp") = false);
    spatialDetectionStreamer.def("publish", &SpatialDetectionStreamer::publish);

    py::class_<DetectionStreamer, std::shared_ptr<DetectionStreamer>> detectionStreamer(m, "DetectionStreamer");
    detectionStreamer.def(py::init([](rclcpp::Node::SharedPtr node,
                                      const std::string& topicName,
                                      std::string frameName,
                                      int width,
                                      int height,
                                      bool normalized = false,
                                      bool getBaseDeviceTimestamp = false) {
                              return std::make_shared<DetectionStreamer>(node, topicName, frameName, width, height, normalized, getBaseDeviceTimestamp);
                          }),
                          py::arg("node"),
                          py::arg("topic_name"),
                          py::arg("frame_name"),
                          py::arg("width"),
                          py::arg("height"),
                          py::arg("normalized") = false,
                          py::arg("get_base_device_timestamp") = false);
    detectionStreamer.def("publish", &DetectionStreamer::publish);

    py::class_<TrackedFeaturesStreamer, std::shared_ptr<TrackedFeaturesStreamer>> trackedFeaturesStreamer(m, "TrackedFeaturesStreamer");
    trackedFeaturesStreamer.def(
        py::init([](rclcpp::Node::SharedPtr node, const std::string& topicName, std::string frameName, bool getBaseDeviceTimestamp = false) {
            return std::make_shared<TrackedFeaturesStreamer>(node, topicName, frameName, getBaseDeviceTimestamp);
        }),
        py::arg("node"),
        py::arg("topic_name"),
        py::arg("frame_name"),
        py::arg("get_base_device_timestamp") = false);
    trackedFeaturesStreamer.def("publish", &TrackedFeaturesStreamer::publish);
};
}  // namespace ros
}  // namespace dai