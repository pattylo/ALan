/*
    This file is part of ALan - the non-robocentric dynamic landing system for quadrotor

    ALan is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ALan is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ALan.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * \file planner_server.cpp
 * \date 01/11/2022
 * \author pattylo
 * \copyright (c) AIRO-LAB, RCUAS of Hong Kong Polytechnic University
 * \brief classes for alan_landing_planning FSM
 */

#include "include/planner_server.h"
// #include <mavros_msgs/qua

planner_server::planner_server(ros::NodeHandle& _nh, int pub_freq)
: nh(_nh), last_request(ros::Time::now().toSec()), _pub_freq(pub_freq)
{
    //subscribe
    uav_state_sub = nh.subscribe<mavros_msgs::State>
            ("/uav/mavros/state", 1, &planner_server::uavStateCallback, this);
    
    uav_AlanPlannerMsg_sub = nh.subscribe<alan_landing_planning::AlanPlannerMsg>
            ("/alan_state_estimation/msgsync/uav/alan_planner_msg", 1, &planner_server::uavAlanMsgCallback, this);

    ugv_AlanPlannerMsg_sub = nh.subscribe<alan_landing_planning::AlanPlannerMsg>
            ("/alan_state_estimation/msgsync/ugv/alan_planner_msg", 1, &planner_server::ugvAlanMsgCallback, this);

    ugv_pose_predict_sub = nh.subscribe<geometry_msgs::PoseStamped>
            ("/alan_ugv/pose_predicted", 1, &planner_server::ugvPosePredictedMsgCallback, this);

    sfc_sub = nh.subscribe<alan_visualization::PolyhedronArray>
            ("/alan_state_estimation/msgsync/polyhedron_array", 1, &planner_server::sfcMsgCallback, this);

    //publish
    pub_fsm = nh.advertise<alan_landing_planning::StateMachine>
            ("/alan_fsm", 1, true);

    local_pos_pub = nh.advertise<geometry_msgs::PoseStamped>
            ("/uav/mavros/setpoint_position/local", 1, true);
        //   /mavros/setpoint_position/local
    local_vel_pub = nh.advertise<geometry_msgs::Twist>
            ("/uav/mavros/setpoint_velocity/cmd_vel_unstamped", 5, true);

    traj_pub = nh.advertise<alan_landing_planning::Traj>
            ("/alan_visualization/traj", 1, true);

    trajArray_pub = nh.advertise<alan_landing_planning::TrajArray>
            ("/alan_visualization/trajArray", 1, true);

    ctrl_pt_pub = nh.advertise<alan_landing_planning::Traj>
            ("/alan_visualization/ctrlPts", 1, true);
    
    kill_attitude_target_pub = nh.advertise<mavros_msgs::AttitudeTarget>
            ("/uav/mavros/setpoint_raw/attitude", 1, true);
        
    //client
    uav_arming_client = nh.serviceClient<mavros_msgs::CommandBool>
            ("/uav/mavros/cmd/arming");

    uav_set_mode_client = nh.serviceClient<mavros_msgs::SetMode>
            ("/uav/mavros/set_mode");
    
    config(nh);

    ros::Rate rate(_pub_freq);
    while(ros::ok())
    {
        if(
            land_traj_constraint_initiated
        )
        {
            std::cout<<"hi"<<std::endl;
            break;
        }
        ros::spinOnce();
        rate.sleep();
    }
}

planner_server::~planner_server()
{

}

void planner_server::mainserver()
{
    std::thread thread_prerequisite_obj(&planner_server::set_alan_b_traj_prerequisite, this);   
    
    //define lambda here for mainloop
    auto mainloop = [this]() mutable
    {
        ros::Rate rate(this->_pub_freq); 
        while(ros::ok())
        {
            // if()
            // std::cout<<"gan"<<std::endl;
            if(
                this->uav_current_state_inititaed     &&
                this->uav_traj_pose_initiated         &&
                this->ugv_traj_pose_initiated                
            )
            {        
                // std::cout<<"hi"<<std::endl;
                this->fsm_manager();
                this->planner_pub();            
            }        
            ros::spinOnce();
            rate.sleep();
        }
    };

    std::thread thread_mainloop_obj(mainloop);

    thread_prerequisite_obj.join();
    thread_mainloop_obj.join();
}

void planner_server::uavStateCallback(const mavros_msgs::State::ConstPtr& msg)
{
    uav_current_state = *msg;
    uav_current_state_inititaed = true;
}

void planner_server::uavAlanMsgCallback(const alan_landing_planning::AlanPlannerMsg::ConstPtr& msg)
{
    uav_current_AlanPlannerMsg = *msg;

    Eigen::Translation3d t_(
        uav_current_AlanPlannerMsg.position.x,
        uav_current_AlanPlannerMsg.position.y,
        uav_current_AlanPlannerMsg.position.z
    );

    Eigen::Quaterniond q_(
        uav_current_AlanPlannerMsg.orientation.ow,
        uav_current_AlanPlannerMsg.orientation.ox,
        uav_current_AlanPlannerMsg.orientation.oy,
        uav_current_AlanPlannerMsg.orientation.oz
    );

    // std::cout<<q2rpy(q_)<<std::endl<<std::endl;
    // q_.
    // tfScalar yaw, pitch, roll;
    // tf::Quaternion q_tf;
    // q_tf.setW(q_.w()); 
    // q_tf.setX(q_.x()); 
    // q_tf.setY(q_.y()); 
    // q_tf.setZ(q_.z()); 

    // tf::Matrix3x3 mat(q_tf);
    // mat.getEulerYPR(yaw, pitch, roll);

    // std::cout<<yaw<<" "<<pitch<<" "<<roll<<std::endl;
    
    uavOdomPose = t_ * q_;

    uav_traj_pose.head<3>() = t_.translation();
    uav_traj_pose(3) = atan2(q_.toRotationMatrix()(1,0), q_.toRotationMatrix()(0,0));

    uav_traj_pose_initiated = true;
}

void planner_server::ugvAlanMsgCallback(const alan_landing_planning::AlanPlannerMsg::ConstPtr& msg)
{
    ugv_current_AlanPlannerMsg = *msg;

    Eigen::Translation3d t_(
        ugv_current_AlanPlannerMsg.position.x,
        ugv_current_AlanPlannerMsg.position.y,
        ugv_current_AlanPlannerMsg.position.z
    );

    Eigen::Quaterniond q_(
        ugv_current_AlanPlannerMsg.orientation.ow,
        ugv_current_AlanPlannerMsg.orientation.ox,
        ugv_current_AlanPlannerMsg.orientation.oy,
        ugv_current_AlanPlannerMsg.orientation.oz
    );
    
    ugvOdomPose = t_ * q_;

    ugv_traj_pose.head<3>() = t_.translation();
    ugv_traj_pose(3) = atan2(q_.toRotationMatrix()(1,0), q_.toRotationMatrix()(0,0));

    ugv_traj_pose_initiated = true;

    if(uav_traj_pose_initiated && ugv_traj_pose_initiated)
    {
        setRelativePose();
    }
}

void planner_server::setRelativePose()
{
    uav_in_ugv_frame_posi =
        ugvOdomPose.rotation().inverse() * 
        (uavOdomPose.translation() - ugvOdomPose.translation());

    // std::cout<<uav_in_ugv_frame_posi<<std::endl<<std::endl;
}

void planner_server::ugvPosePredictedMsgCallback(const geometry_msgs::PoseStamped::ConstPtr& msg)
{
    Eigen::Translation3d t_(
        msg->pose.position.x,
        msg->pose.position.y,
        msg->pose.position.z
    );

    Eigen::Quaterniond q_(
        msg->pose.orientation.w,
        msg->pose.orientation.x,
        msg->pose.orientation.y,
        msg->pose.orientation.z
    );

    ugvOdomPose_predicted = t_ * q_;
    
    ugv_traj_predict_pose.head<3>() = t_.translation();
    ugv_traj_predict_pose(3) = atan2(q_.toRotationMatrix()(1,0), q_.toRotationMatrix()(0,0));

}

void planner_server::sfcMsgCallback(const alan_visualization::PolyhedronArray::ConstPtr& msg)
{
    land_traj_constraint.a_series_of_Corridor.clear();
    land_traj_constraint = *msg;

    land_traj_constraint_initiated = true;
}


void planner_server::fsm_manager()
{
    if(fsm_state == IDLE)
    {
        if(print_or_not)
        {
            ROS_YELLOW_STREAM(IDLE);
            print_or_not = false;
        }
        if(get_ready())
        {
            fsm_state = ARMED;
            print_or_not = true;

            takeoff_hover_pt.x = uav_traj_pose(0);
            takeoff_hover_pt.y = uav_traj_pose(1);
            takeoff_hover_pt.z = take_off_height + ugv_height;

            target_traj_pose(0) = takeoff_hover_pt.x;
            target_traj_pose(1) = takeoff_hover_pt.y;
            target_traj_pose(2) = takeoff_hover_pt.z;
            target_traj_pose(3) = ugv_traj_pose(3);

            std::cout<<"target takeoff position\n"<<target_traj_pose<<std::endl<<std::endl;
        }
    }
    else if(fsm_state == ARMED)
    {
        if(print_or_not)
        {
            ROS_GREEN_STREAM(ARMED);
            print_or_not = false;
        }
        if(taking_off())
        {
            fsm_state = TOOKOFF;
            print_or_not = true;
            last_request = ros::Time::now().toSec();
        }

    }
    else if(fsm_state == TOOKOFF)
    {
        if(print_or_not)
        {
            ROS_GREEN_STREAM(TOOKOFF);
            print_or_not = false;
        }
        if(ros::Time::now().toSec() - last_request > ros::Duration(2.0).toSec())
        {
            fsm_state = FOLLOW;
            print_or_not = true;
            last_request = ros::Time::now().toSec();
        }

    }
    else if(fsm_state == FOLLOW)
    {
        if(print_or_not)
        {
            ROS_GREEN_STREAM(FOLLOW);
            print_or_not = false;
        }
        if(go_to_rendezvous_pt_and_follow())
        {
            fsm_state = RENDEZVOUS;
            print_or_not = true;
            last_request = ros::Time::now().toSec();            
        }

    }
    else if(fsm_state == RENDEZVOUS)
    {
        if(print_or_not)
        {
            ROS_GREEN_STREAM(RENDEZVOUS);
            print_or_not = false;
        }        
        if(rendezvous())
        {
            fsm_state = LAND;
            print_or_not = true;
            last_request = ros::Time::now().toSec();

            plan_traj = true;
        }

    }
    else if(fsm_state == LAND)
    {
        if(print_or_not)
        {
            ROS_CYAN_STREAM(LAND);
            print_or_not = false;
        }

        if(land())
        {
            fsm_state = SHUTDOWN;
            print_or_not = true;
            last_request = ros::Time::now().toSec();
        }

    }
    else if(fsm_state == SHUTDOWN)
    {
        if(print_or_not)
        {
            ROS_YELLOW_STREAM(SHUTDOWN);
            print_or_not = false;
        }
        if(shutdown())
        {
            fsm_state = MISSION_COMPLETE;
            print_or_not = true;
            last_request = ros::Time::now().toSec();
        }
    }
    else if(fsm_state == MISSION_COMPLETE)
    {
        if(print_or_not)
        {
            ROS_GREEN_STREAM(MISSION_COMPLETE);
            print_or_not = false;
        }
        ROS_GREEN_STREAM("NOW WAIT FOR EXIT...");
    }
    else
    {
        ROS_ERROR("Please Check System...");
    }

}

bool planner_server::get_ready()
{
    bool return_state = false;
    if( uav_current_state.mode != "OFFBOARD" &&
            (ros::Time::now().toSec() - last_request > ros::Duration(2.0).toSec()))
        {
            if( uav_set_mode_client.call(uav_set_mode) &&
                uav_set_mode.response.mode_sent)
            {
                ROS_INFO("Offboard enabled");
                alan_fsm_object.finite_state_machine = ARMED;
            }   
            last_request = ros::Time::now().toSec();
        } 
    else 
    {
        if( !uav_current_state.armed &&
            (ros::Time::now().toSec() - last_request > ros::Duration(2.0).toSec()))
        {
            if( uav_arming_client.call(arm_cmd) &&
                arm_cmd.response.success)
            {
                ROS_INFO("Vehicle armed");
                alan_fsm_object.finite_state_machine = TOOKOFF;
                return_state = true;
            }
            last_request = ros::Time::now().toSec();
        }
    }

    if(return_state)
        return true;
    else
        return false;

}

bool planner_server::taking_off()
{
    double dis = 
        std::pow(
            uav_current_AlanPlannerMsg.position.x - takeoff_hover_pt.x,
            2
        )
        + std::pow(
            uav_current_AlanPlannerMsg.position.y - takeoff_hover_pt.y,
            2
        )
        + std::pow(
            uav_current_AlanPlannerMsg.position.z - takeoff_hover_pt.z,
            2
        );
    
    dis = std::sqrt(dis);
    // std::cout<<dis<<std::endl;
    if(dis < 0.15)
        return true;
    else    
        return false;
}

bool planner_server::go_to_rendezvous_pt_and_follow()
{
    //follow dynamic
    // ++++++++++
    // target_traj_pose = set_following_target_pose();
    // std::cout<<uav_in_ugv_frame_posi.norm() - following_norm<<std::endl;

    // if(
    //     uav_in_ugv_frame_posi.norm() - following_norm < 0.4 &&
    //     prerequisite_set
    // )
    //     return true;
    // else
    //     return false;  
    // ++++++++++
    
    //perform block traj during experiment
    target_traj_pose = set_uav_block_pose();
    return false;

    //decide whether to land based on following quality
    // if(
    //     uav_current_AlanPlannerMsg.good2fly && 
    //     (ros::Time::now().toSec() - last_request) > ros::Duration(2.0).toSec()
    // )
    // {
    //     // std::cout<<"good to fly"<<std::endl;
    //     return true;
    // }
    // else
    // {
    //     // std::cout<<"not good to fly"<<std::endl;
    //     return false;
    // }
    // if(set_alan_b_traj_prerequisite)


          

}

bool planner_server::rendezvous()
{
    target_traj_pose = set_following_target_pose();
    
    if(ros::Time::now().toSec() - last_request > ros::Duration(4.0).toSec())
        return true;
    else 
        return false;

}

bool planner_server::hover()
{
    target_traj_pose(0) = 0.0;
    target_traj_pose(1) = 0.0;
    target_traj_pose(2) = 1.5;
    target_traj_pose(3) = M_PI;

    return false;
}

bool planner_server::land()
{        
    if(plan_traj)
    {        
        set_alan_b_traj_online();
        plan_traj = false;
    }

    if(
        traj_i < optimal_traj_info_obj.optiTraj.trajectory.size()
    )
    {
        if(uav_in_ugv_frame_posi.head<2>().norm() > 0.0)
        {
            target_traj_pose(0) = optimal_traj_info_obj.optiTraj.trajectory[traj_i].position.x;
            target_traj_pose(1) = optimal_traj_info_obj.optiTraj.trajectory[traj_i].position.y;
            target_traj_pose(2) = optimal_traj_info_obj.optiTraj.trajectory[traj_i].position.z;

            target_traj_pose.head<3>() 
                = ugvOdomPose_predicted.rotation() * target_traj_pose.head<3>() + ugvOdomPose_predicted.translation();

            target_traj_pose(3) = ugv_traj_pose(3);

            traj_i++;
            
            return false;

        }
        else
        {
            ROS_GREEN_STREAM("CUT!!!!!!");
            return true;
        }        

    }
    else
    {
        std::cout<<uav_in_ugv_frame_posi.head<2>().norm()<<std::endl;

        if(uav_in_ugv_frame_posi.head<2>().norm() > 0.16)
        {
            target_traj_pose(0) = optimal_traj_info_obj.optiTraj.trajectory[traj_i - 1].position.x;
            target_traj_pose(1) = optimal_traj_info_obj.optiTraj.trajectory[traj_i - 1].position.y;
            target_traj_pose(2) = optimal_traj_info_obj.optiTraj.trajectory[traj_i - 1].position.z;

            target_traj_pose.head<3>() 
            = ugvOdomPose_predicted.rotation() * target_traj_pose.head<3>() + ugvOdomPose_predicted.translation();
            
            target_traj_pose(3) = ugv_traj_pose(3);

            return false;
        }
        else 
        {
            ROS_GREEN_STREAM("NEXT!!!");
            return true;
        }
            
    }

}

bool planner_server::shutdown()
{
    kill_or_not = true;
    ROS_WARN("UAV NOW SHUTDOWN...");
    attitude_target_for_kill.thrust = 0;

    Eigen::Vector2d temp = uav_in_ugv_frame_posi.head<2>();

    if(temp.norm() < 0.1)
        return true;
    else
        return false;
}

void planner_server::planner_pub()
{
    alan_fsm_object.finite_state_machine = fsm_state;
    pub_fsm.publish(alan_fsm_object);

    if(prerequisite_set)
    {
        // std::cout<<"hi"<<std::endl;
        traj_pub.publish(optimal_traj_info_obj.optiTraj);
        trajArray_pub.publish(optimal_traj_info_obj.optiTrajArray);        
        ctrl_pt_pub.publish(optimal_traj_info_obj.ctrl_pts_optimal);
    }
    // std::cout<<uav_traj_desired.pose.position.x<<std::endl;

    Eigen::Vector4d twist_result = pid_controller(uav_traj_pose, target_traj_pose);

    uav_traj_twist_desired.linear.x = twist_result(0);
    uav_traj_twist_desired.linear.y = twist_result(1);
    uav_traj_twist_desired.linear.z = twist_result(2);
    uav_traj_twist_desired.angular.z = twist_result(3);


    if(fsm_state != SHUTDOWN && fsm_state != MISSION_COMPLETE)
        local_vel_pub.publish(uav_traj_twist_desired);
    else
        kill_attitude_target_pub.publish(attitude_target_for_kill);
}

Eigen::Vector4d planner_server::pid_controller(Eigen::Vector4d pose, Eigen::Vector4d setpoint)
{
    Eigen::Vector4d error, u_p, u_i, u_d, output, derivative;
    // error[0] = 1;
    double iteration_time = ros::Time::now().toSec() - pid_last_request;

    if(iteration_time > 1)
    {
        pid_last_request = ros::Time::now().toSec();
        return Eigen::Vector4d(0, 0, 0, 0);
    }
        
    // Eigen::Vector4d K_p(2.0, 2.0, 1.5, 1);
    // Eigen::Vector4d K_i(0.05, 0.05, 0.05, 0.05);
    // Eigen::Vector4d K_d(0, 0, 0, 0);

    error = setpoint - pose;


    if (error(3) >= M_PI)
    {
        error(3) -= 2 * M_PI;
    }

    if (error(3) <= -M_PI)
    {
        error(3) += 2  *M_PI;
    }

    for (int i = 0; i < 4; i++)
    { 
        //i = x,y,z
        integral(i) += (error(i) * iteration_time);

        if(integral(i) >  1)
        { 
            integral(i) = 1;
        }

        if(integral(i) < -1)
        { 
            integral(i) = -1;
        }

        derivative(i) = (error(i) - last_error(i)) / (iteration_time + 1e-10);

        u_p(i) = error(i) * kp(i);        //P controller
        u_i(i) = integral(i) * ki(i);     //I controller
        u_d(i) = derivative(i) * kd(i);   //D controller

        output(i) = u_p(i) + u_i(i) + u_d(i);
        
    }

    for (int i = 0; i < 3; i++)
    {
        if(output(i) >  v_max)
            { 
                output(i) =  v_max;
            }

        if(output(i) < -v_max)
        { 
            output(i) = -v_max;
        }
    }

    last_error = error;
    pid_last_request = ros::Time::now().toSec();

    return output;
}

Eigen::Vector4d planner_server::set_following_target_pose()
{
    Eigen::Vector3d uav_following_pt = Eigen::Vector3d(-landing_horizontal, 0.0, take_off_height);    
    uav_following_pt =  ugvOdomPose.rotation() * uav_following_pt 
        + Eigen::Vector3d(
            ugvOdomPose.translation().x(),
            ugvOdomPose.translation().y(),
            ugvOdomPose.translation().z()
        );

    // uav_following_pt.x() = ugv_traj_pose(0);
    // uav_following_pt.y() = ugv_traj_pose(1);
    // uav_following_pt.z() = ugv_traj_pose(2) + take_off_height;

    Eigen::Vector4d following_target_pose;
    following_target_pose(0) = uav_following_pt(0);
    following_target_pose(1) = uav_following_pt(1);
    following_target_pose(2) = uav_following_pt(2);
    following_target_pose(3) = ugv_traj_pose(3);

    return following_target_pose;
}

Eigen::Vector4d planner_server::set_uav_block_pose()
{    
    if(set_block_traj)
    { 
        double vel = 0.2;

        std::cout<<"take_off_height: "<<take_off_height<<std::endl;

        Eigen::Vector3d v1 = Eigen::Vector3d(-0.0,  0.2, take_off_height);
        Eigen::Vector3d v2 = Eigen::Vector3d(-0.0, -0.2, take_off_height);
        Eigen::Vector3d v3 = Eigen::Vector3d(-2.0, -0.2, take_off_height);
        Eigen::Vector3d v4 = Eigen::Vector3d(-2.0,  0.2, take_off_height);

        std::vector<Eigen::Vector3d> traj_per_edge;
        
        //v1-v2
        double delta_distance = (v1 - v2).norm();
        int total_time_steps = delta_distance / vel * _pub_freq;

        for(int i = 0; i < total_time_steps; i++)
        {
            traj_per_edge.emplace_back(Eigen::Vector3d(
                v1.x(),
                v1.y() + (v2.y() - v1.y()) / total_time_steps * (i + 1),
                v1.z()
            ));
        }
        block_traj_pts.emplace_back(traj_per_edge);

        //v2-v3
        traj_per_edge.clear();
        delta_distance = (v2 - v3).norm();
        total_time_steps = delta_distance / vel * _pub_freq;

        for(int i = 0; i < total_time_steps; i++)
        {
            traj_per_edge.emplace_back(Eigen::Vector3d(
                v2.x() + (v3.x() - v2.x()) / total_time_steps * (i + 1),
                v2.y(),
                v2.z()
            ));
        }
        block_traj_pts.emplace_back(traj_per_edge);

        //v3-v4
        traj_per_edge.clear();
        delta_distance = (v3 - v4).norm();
        total_time_steps = delta_distance / vel * _pub_freq;

        for(int i = 0; i < total_time_steps; i++)
        {
            traj_per_edge.emplace_back(Eigen::Vector3d(
                v3.x(),
                v3.y() + (v4.y() - v3.y()) / total_time_steps * (i + 1),
                v3.z()
            ));
        }
        block_traj_pts.emplace_back(traj_per_edge);

        //v4-v1
        traj_per_edge.clear();
        delta_distance = (v1 - v4).norm();
        total_time_steps = delta_distance / vel * _pub_freq;

        for(int i = 0; i < total_time_steps; i++)
        {
            traj_per_edge.emplace_back(Eigen::Vector3d(
                v4.x() + (v1.x() - v4.x()) / total_time_steps * (i + 1),
                v4.y(),
                v4.z()
            ));
        }
        block_traj_pts.emplace_back(traj_per_edge);

        std::cout<<"total edge:..."<<block_traj_pts.size()<<std::endl;
        std::cout<<"v1-v2:........"<<block_traj_pts[0].size()<<std::endl;
        std::cout<<"v2-v3:........"<<block_traj_pts[1].size()<<std::endl;
        std::cout<<"v3-v4:........"<<block_traj_pts[2].size()<<std::endl;
        std::cout<<"v4-v1:........"<<block_traj_pts[3].size()<<std::endl;


        //repeat 4 times        

        for(int i = 0; i < 4; i++)
        {
            block_traj_pts.emplace_back(block_traj_pts[i]);
        }

        for(int i = 0; i < 4; i++)
        {
            block_traj_pts.emplace_back(block_traj_pts[i]);
        }

        for(int i = 0; i < 4; i++)
        {
            block_traj_pts.emplace_back(block_traj_pts[i]);
        }
        
        std::cout<<"total edge:..."<<block_traj_pts.size()<<std::endl;

        set_block_traj = false;
        wp_counter_i = 0;
        traj_counter_j = 0;
        // std::cout<<block_traj_pts[0][0].x()<<std::endl;
        // std::cout<<block_traj_pts[0][0].y()<<std::endl;
        // std::cout<<block_traj_pts[0][0].z()<<std::endl;
    }

    Eigen::Vector4d following_target_pose;
    // std::cout<<traj_counter_j<<std::endl;
    // std::cout<<wp_counter_i<<std::endl<<std::endl;

    if(traj_counter_j == block_traj_pts[wp_counter_i].size())
    {
        wp_counter_i++;
        traj_counter_j = 0;
        landing_hover_pt.x = uav_traj_pose(0);
        landing_hover_pt.y = uav_traj_pose(1);
        landing_hover_pt.z = uav_traj_pose(2);
        landing_hover_pt.yaw = ugv_traj_pose(3);
    }
        
    
    if(wp_counter_i == block_traj_pts.size())
    {
        following_target_pose(0) = landing_hover_pt.x;
        following_target_pose(1) = landing_hover_pt.y;
        following_target_pose(2) = landing_hover_pt.z;
        following_target_pose(3) = landing_hover_pt.yaw;

        return following_target_pose;
    }
    else 
    {
        Eigen::Vector3d uav_following_pt;
        uav_following_pt.x() = block_traj_pts[wp_counter_i][traj_counter_j].x();
        uav_following_pt.y() = block_traj_pts[wp_counter_i][traj_counter_j].y();
        uav_following_pt.z() = block_traj_pts[wp_counter_i][traj_counter_j].z();

        uav_following_pt =  ugvOdomPose.rotation() * uav_following_pt 
        + Eigen::Vector3d(
            ugvOdomPose.translation().x(),
            ugvOdomPose.translation().y(),
            ugvOdomPose.translation().z()
        );

        traj_counter_j ++;
        // std::cout<<traj_counter_j<<std::endl;
        // std::cout<<"here"<<std::endl;
        following_target_pose(0) = uav_following_pt.x();
        following_target_pose(1) = uav_following_pt.y();
        following_target_pose(2) = uav_following_pt.z();
        following_target_pose(3) = ugv_traj_pose(3);

        // std::cout<<following_target_pose<<std::endl<<std::endl;


        return following_target_pose;
        
    }

}

void planner_server::config(ros::NodeHandle& _nh)
{
    ROS_INFO("Planner Server Launch...\n");
    nh.getParam("/alan_master_planner_node/axis_dim", axis_dim);
    nh.getParam("/alan_master_planner_node/n_order", n_order);
    nh.getParam("/alan_master_planner_node/m", m);
    nh.getParam("/alan_master_planner_node/d_order", d_order);

    nh.getParam("/alan_master_planner_node/PID_gain", pid_gain_list);

    std::cout<<pid_gain_list.size()<<std::endl;
    std::cout<<"here! hi:..."<<std::endl;
    for(int i = 0; i < pid_gain_list.size(); i++)
    {
        std::cout<<"hi...."<<i<<std::endl;
        if(i == 0)
        {
            kp(0) = pid_gain_list[i]["x"];
            kp(1) = pid_gain_list[i]["y"];
            kp(2) = pid_gain_list[i]["z"];
            kp(3) = pid_gain_list[i]["yaw"];
        }
        else if(i == 1)
        {
            ki(0) = pid_gain_list[i]["x"];
            ki(1) = pid_gain_list[i]["y"];
            ki(2) = pid_gain_list[i]["z"];
            ki(3) = pid_gain_list[i]["yaw"];
        }
        else if(i == 2)
        {
            kd(0) = pid_gain_list[i]["x"];
            kd(1) = pid_gain_list[i]["y"];
            kd(2) = pid_gain_list[i]["z"];
            kd(3) = pid_gain_list[i]["yaw"];
        }
        std::cout<<i<<std::endl;            
    }

    std::cout<<"pid_gains..."<<std::endl;
    std::cout<<kp<<std::endl<<std::endl;
    std::cout<<ki<<std::endl<<std::endl;
    std::cout<<kd<<std::endl<<std::endl;

    nh.getParam("/alan_master_planner_node/landing_velocity", uav_landing_velocity);
    nh.getParam("/alan_master_planner_node/ugv_height", ugv_height);
    
    nh.getParam("/alan_master_planner_node/take_off_height", take_off_height);
    nh.getParam("/alan_master_planner_node/landing_horizontal", landing_horizontal);
    nh.getParam("/alan_master_planner_node/touch_down_height", touch_down_height);
    nh.getParam("/alan_master_planner_node/touch_down_height", touch_down_offset);


    nh.getParam("/alan_master_planner_node/v_max", v_max);
    nh.getParam("/alan_master_planner_node/a_max", a_max);

    nh.getParam("/alan_master_planner_node/sample_square_root", sample_square_root);
    
    landing_time_duration_max 
        = (take_off_height - touch_down_height + landing_horizontal) / uav_landing_velocity;
    landing_time_duration_min 
        = (std::sqrt(std::pow(take_off_height - touch_down_height,2) + std::pow(landing_horizontal,2))) / v_max;
    std::cout<<"time here: "<<landing_time_duration_max<<" "<<landing_time_duration_min<<std::endl;
    following_norm = std::pow(landing_horizontal,2) + std::pow(take_off_height,2);
    following_norm = std::sqrt(following_norm);

    nh.getParam("/alan_master_planner_node/log_path", log_path);

    std::cout<<"v_max: "<<v_max<<std::endl;
    std::cout<<"a_max: "<<a_max<<std::endl;

    nh.getParam("/alan_master/final_corridor_height", final_corridor_height);
    nh.getParam("/alan_master/final_corridor_length", final_corridor_length);

    uav_set_mode.request.custom_mode = "OFFBOARD";
    arm_cmd.request.value = true;
    alan_fsm_object.finite_state_machine = IDLE;

    //block traj temp
    std::cout<<"set block traj..."<<set_block_traj<<std::endl;
}

void planner_server::set_alan_b_traj_prerequisite()
{
    // std::cout<<"set_pre_requisite...";
    // std::cout<<land_traj_constraint_initiated<<std::endl;
    if(land_traj_constraint_initiated)
    {
        btraj_info.axis_dim = axis_dim;
        btraj_info.n_order = n_order;
        btraj_info.m = m;
        btraj_info.d_order = d_order;

        btraj_dconstraints.v_max(0) = v_max;
        btraj_dconstraints.v_min(0) = -v_max;
        btraj_dconstraints.a_max(0) = a_max;
        btraj_dconstraints.a_min(0) = -a_max;

        btraj_dconstraints.v_max(1) = v_max;
        btraj_dconstraints.v_min(1) = -v_max;
        btraj_dconstraints.a_max(1) = a_max;
        btraj_dconstraints.a_min(1) = -a_max;

        btraj_dconstraints.v_max(2) = v_max;
        btraj_dconstraints.v_min(2) = -v_max;
        btraj_dconstraints.a_max(2) = a_max;
        btraj_dconstraints.a_min(2) = -a_max;

        set_btraj_inequality_kinematic();

        std::cout<<"here are the corridors...";
        std::cout<<corridors.size()<<std::endl;

        btraj_constraints.sfc_list = corridors;
        btraj_constraints.d_constraints = btraj_dconstraints;
        btraj_constraints.corridor_type = "POLYH";

        alan_btraj_sample = new alan_traj::traj_sampling(
            btraj_info,
            btraj_constraints,
            _pub_freq,
            log_path
        );

        std::vector<double> time_sample;
        time_sample.emplace_back(landing_time_duration_min);
        time_sample.emplace_back(landing_time_duration_max);        

        Eigen::Vector3d posi_start(
            -landing_horizontal,
            0.0,
            take_off_height
        );

        Eigen::Vector3d posi_end(
            touch_down_offset,
            0.0,
            touch_down_height
        );

        Eigen::Vector3d velo_constraint(0.0,0.0,0.0);

        std::cout<<"here are the starting point:"<<std::endl;
        std::cout<<posi_start<<std::endl;
        std::cout<<posi_end<<std::endl<<std::endl;

        std::cout<<sample_square_root<<std::endl;

        double tick0 = ros::Time::now().toSec();
        alan_btraj_sample->set_prerequisite(time_sample, sample_square_root, sample_square_root);        
        alan_btraj_sample->updateBoundary(posi_start, posi_end, velo_constraint);
        double tock0 = ros::Time::now().toSec();

        double tick1 = ros::Time::now().toSec();
        alan_btraj_sample->optSamples();
        optimal_traj_info_obj = alan_btraj_sample->getOptimalTrajInfo();        
        double tock1 = ros::Time::now().toSec();

        std::cout<<"set Matrices..."<<std::endl;
        std::cout<<"ms: "<<(tock0 - tick0) * 1000<<std::endl;
        std::cout<<"fps: "<<1 / (tock0 - tick0)<<std::endl<<std::endl;

        std::cout<<"set Optimization Sample..."<<std::endl;
        std::cout<<"ms: "<<(tock1 - tick1) * 1000<<std::endl;
        std::cout<<"fps: "<<1 / (tock1 - tick1)<<std::endl;


        //test online here...

        // std::cout<<"planner_server optimal time here..."<<optimal_time.size()<<std::endl;
        // for(auto what : optimal_traj_info_obj.optimal_time_allocation)
        // {
        //     std::cout<<what<<std::endl;
        // }

        std::cout<<"after setting pre-requisite..."<<std::endl;

        std::cout<<uav_in_ugv_frame_posi<<std::endl;
        std::cout<<std::endl<<std::endl;
        std::cout<<"now online..."<<std::endl;
        // btraj_sampling.
        
        Eigen::Vector3d posi_goal(touch_down_offset,0,touch_down_height);
        double tick2 = ros::Time::now().toSec();
        alan_landing_planning::Traj traj_execute_final_in_B = alan_btraj_sample->opt_traj_online(
            posi_start,
            posi_end
        );
        double tock2 = ros::Time::now().toSec();

        std::cout<<"online update:"<<std::endl;
        std::cout<<"ms: "<<(tock2 - tick2) * 1000<<std::endl;
        std::cout<<"fps: "<<1 / (tock2 - tick2)<<std::endl;

        if(optimal_traj_info_obj.got_heuristic_optimal)
            prerequisite_set = true;
        else
            prerequisite_set = false;

    }
    else
    {
        prerequisite_set = false;
        return;
    }

}

void planner_server::set_btraj_inequality_kinematic()
{
    int temp_size_i = 0;

    std::cout<<"we got this many corridor:..."<<land_traj_constraint.a_series_of_Corridor.size()<<std::endl;
    
    corridors.clear();

    for(int i = 0; i < land_traj_constraint.a_series_of_Corridor.size(); i++)
    {
        temp_size_i = land_traj_constraint.a_series_of_Corridor[i].PolyhedronTangentArray.size();
        
        temp_poly.PolyhedronTangentArray.clear();

        for(int j = 0; j < temp_size_i; j++)
        {
            temp_poly.PolyhedronTangentArray.emplace_back(
                land_traj_constraint.a_series_of_Corridor[i].PolyhedronTangentArray[j]
            );
        }

        corridors.emplace_back(temp_poly);
    }

    btraj_constraints.sfc_list = corridors;
    btraj_constraints.corridor_type = "POLYH";

}

void planner_server::set_alan_b_traj_online()
{
    Eigen::Vector3d posi_current = uav_in_ugv_frame_posi;
    Eigen::Vector3d posi_goal(touch_down_offset,0,touch_down_height);

    traj_execute_final_in_B = alan_btraj_sample->opt_traj_online(
        posi_current,
        posi_goal
    );

    //set landing trajectory indicator to 0
    traj_i = 4;

}