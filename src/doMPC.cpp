/** Looks at desired goal, and generates optimum sterring input based on least collisions **/

#include "mpc_auto/doMPC.h"

Vars vars;
Params params;
Workspace work;
Settings settings;

#include "mpc_auto/cvxgen_mpc.h"

mpcBlock::doMPC::doMPC() {
    nodeH = ros::NodeHandle(); //Initialize node

    getParams();

    lidar_sub = nodeH.subscribe(laser_topic, 10, &mpcBlock::doMPC::lidar_callback, this);

    drive_pub = nodeH.advertise<ackermann_msgs::AckermannDriveStamped>(drive_topic, 1); //publishes steering angle and velocity

    marker_x_subs = nodeH.subscribe(marker_x_topic, 10, &mpcBlock::doMPC::marker_x_callback, this); //subscribe x-waypoint
    marker_y_subs = nodeH.subscribe(marker_y_topic, 10, &mpcBlock::doMPC::marker_y_callback, this); //subscribe y-waypoint

    current_scan.y_lower_distance = -1;
    current_scan.y_upper_distance = 1;
}

void mpcBlock::doMPC::getParams() {

    //Get parameters
    nodeH.getParam("laser_topic", laser_topic);
    nodeH.getParam("drive_topic", drive_topic);
    nodeH.getParam("steering_angle_change", steering_angle_change);
    nodeH.getParam("marker_x_topic", marker_x_topic);
    nodeH.getParam("theta_topic", theta_topic);
    nodeH.getParam("marker_y_topic", marker_y_topic);
    nodeH.getParam("steering_limit", steering_limit);
    nodeH.getParam("high_velocity", high_velocity);
    nodeH.getParam("low_velocity", low_velocity);
    nodeH.getParam("lower_threshold", lower_threshold);
    nodeH.getParam("midline_threshold", midline_threshold);
    nodeH.getParam("breakneck_steering", breakneck_steering);
    nodeH.getParam("min_halfspace_width",min_halfspace_width);
    nodeH.getParam("breakneck_steering_threshold", breakneck_steering_threshold);
    nodeH.getParam("Q_matrix_1", Q_matrix_1);
    nodeH.getParam("Q_matrix_2" ,Q_matrix_2);
    nodeH.getParam("R_matrix_1", R_matrix_1);
    nodeH.getParam("B_matrix", B_matrix);
    nodeH.getParam("offset", offset);
}

void mpcBlock::doMPC::marker_x_callback(const std_msgs::Float64::ConstPtr &msg) {
    rot_waypoint_x = msg->data;
}

void mpcBlock::doMPC::marker_y_callback(const std_msgs::Float64::ConstPtr &msg) {
    rot_waypoint_y = msg->data - offset;

}

void mpcBlock::doMPC::lidar_callback(const sensor_msgs::LaserScan::ConstPtr &scan_msg){

    current_scan.angle_increment = scan_msg->angle_increment;
    current_scan.angle_sweep = scan_msg->angle_max - scan_msg->angle_min;

    current_scan.zero_angle = int(0.5*current_scan.angle_sweep/current_scan.angle_increment); //Calculate index for left side 0 degree
    current_scan.left_angle = int(0.75*current_scan.angle_sweep/current_scan.angle_increment); //Calculate index for left side 90 degree
    current_scan.right_angle = int(0.25*current_scan.angle_sweep/current_scan.angle_increment); //Calculate index for 90 degree angle

    int x_lower_index = current_scan.right_angle;
    int x_upper_index = current_scan.left_angle;

    current_scan.y_lower_distance = -scan_msg->ranges[x_lower_index];
    current_scan.y_upper_distance = scan_msg->ranges[x_upper_index];

    for(int i = current_scan.zero_angle; i>=current_scan.right_angle; i--){
        if( !std::isinf(scan_msg->ranges[i]) && !std::isnan(scan_msg->ranges[i]) && scan_msg->ranges[i] < lower_threshold){
            x_lower_index = i;
            break;
        }
    }

    for(int i = current_scan.zero_angle; i<=current_scan.left_angle; i++){
        if(!std::isinf(scan_msg->ranges[i]) && !std::isnan(scan_msg->ranges[i]) && scan_msg->ranges[i] < lower_threshold){
            x_upper_index = i;
            break;
        }
    }

    double y_lower_dist = scan_msg->ranges[x_lower_index]*std::sin(-Pi + 2*Pi*float(x_lower_index)/float(scan_msg->ranges.size()));
    double y_upper_dist = scan_msg->ranges[x_upper_index]*std::sin(-Pi + 2*Pi*float(x_upper_index)/float(scan_msg->ranges.size()));

    current_scan.y_lower_distance = -scan_msg->ranges[x_lower_index];
    current_scan.y_upper_distance = scan_msg->ranges[x_upper_index];

    if(std::abs(current_scan.y_lower_distance) < 0.5){
        ROS_INFO("lower diffused");
        offset = 0.2;
    }

    if(std::abs(current_scan.y_upper_distance) < 0.5){
        ROS_INFO("upper diffused");
        offset = -0.2;
    }

    mpcBlock::doMPC::controller_callback();
}

void mpcBlock::doMPC::controller_callback() {
    current_loop_time = ros::Time::now().toNSec();

    (run_cvxgenOptimization(Q_matrix_1, Q_matrix_2, R_matrix_1, B_matrix));

    steering_angle = run_cvxgenOptimization::solve_mpc(rot_waypoint_y, rot_waypoint_x, current_scan.y_lower_distance, current_scan.y_upper_distance);

    if(std::isnan(steering_angle)){
        ROS_WARN("Steering angle is NAN!");
        steering_angle = prev_steering_angle;
    }

    if( prev_steering_angle*steering_angle < 0 && std::abs(steering_angle) > 0.001 ) {
        steering_angle = (steering_angle + prev_steering_angle) / 2.0;
        prev_steering_angle = steering_angle;
    }

    prev_loop_time = current_loop_time;
}

void mpcBlock::doMPC::publisherCallback() {

    ackermann_msgs::AckermannDriveStamped drive_msg;
    drive_msg.drive.steering_angle = steering_angle; //Sets steering angle
    drive_msg.drive.speed = high_velocity - (high_velocity - low_velocity) * std::abs(steering_angle) / steering_limit; //Interpolation function
    drive_pub.publish(drive_msg); //Sets velocity based on steering angle conditions
}

void mpcBlock::doMPC::debug() { //Prints stuff on console for debugging. Commented out code is temporarily not being debugged

    ROS_INFO("upper: %f", current_scan.y_upper_distance);
    ROS_INFO("lower: %f", current_scan.y_lower_distance);
    ROS_INFO("steering angle: %f", steering_angle);
}

int main(int argc, char ** argv) {
    ros::init(argc, argv, "doMPC");
    mpcBlock::doMPC mpc_class_init;

    ros::Rate loop_rate(100);

    while(ros::ok()){
        mpc_class_init.debug();
        mpc_class_init.publisherCallback();

        ros::spinOnce();
        loop_rate.sleep();
    }

    return 0;
}
