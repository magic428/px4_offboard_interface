/**
 * @file autopilot_interface.cpp
 *
 * @brief Autopilot interface functions
 *
 * Functions for sending and recieving commands to an autopilot via MAVlink
 *
 *
 */


// ------------------------------------------------------------------------------
//   Includes
// ------------------------------------------------------------------------------

#include "autopilot_interface.h"
#include "px4_custom_mode.h"



// ----------------------------------------------------------------------------------
//   Time
// ------------------- ---------------------------------------------------------------
uint64_t
get_time_usec()
{
    static struct timeval _time_stamp;
    gettimeofday(&_time_stamp, NULL);
    return _time_stamp.tv_sec*1000000 + _time_stamp.tv_usec;
}


// ----------------------------------------------------------------------------------
//   Setpoint Helper Functions
// ----------------------------------------------------------------------------------

// choose one of the next three

/*
 * Set target local ned position
 *
 * Modifies a mavlink_set_position_target_local_ned_t struct with target XYZ locations
 * in the Local NED frame, in meters.
 */
void
set_position(float x, float y, float z, mavlink_set_position_target_local_ned_t &sp)
{
    sp.type_mask =
        MAVLINK_MSG_SET_POSITION_TARGET_LOCAL_NED_POSITION;

    sp.coordinate_frame = MAV_FRAME_LOCAL_NED;

    sp.x   = x;
    sp.y   = y;
    sp.z   = z;

    printf("POSITION SETPOINT XYZ = [ %.4f , %.4f , %.4f ] \n", sp.x, sp.y, sp.z);

}

void
set_land(mavlink_set_position_target_local_ned_t &sp)
{
    sp.time_boot_ms = (uint32_t) (get_time_usec()/1000);

    sp.type_mask = 0x2000;  // 0b10 0000 0000 0000
    printf("land cmd send...\n");
}

/*
 * Set target local ned velocity
 *
 * Modifies a mavlink_set_position_target_local_ned_t struct with target VX VY VZ
 * velocities in the Local NED frame, in meters per second.
 */
void
set_position_velocity(float x, float y, float z, float vx, float vy, float vz, mavlink_set_position_target_local_ned_t &sp)
{
    sp.type_mask =
        MAVLINK_MSG_SET_POSITION_TARGET_LOCAL_NED_VELOCITY     ;

    sp.coordinate_frame = MAV_FRAME_LOCAL_NED;

    sp.x   = x;
    sp.y   = y;
    sp.z   = z;

    sp.vx  = vx;
    sp.vy  = vy;
    sp.vz  = vz;

    //printf("VELOCITY SETPOINT UVW = [ %.4f , %.4f , %.4f ] \n", sp.vx, sp.vy, sp.vz);
}

void
set_velocity(float vx, float vy, float vz, mavlink_set_position_target_local_ned_t &sp)
{
    sp.type_mask |=
        MAVLINK_MSG_SET_POSITION_TARGET_LOCAL_NED_VELOCITY     ;

    sp.coordinate_frame = MAV_FRAME_LOCAL_NED;

    sp.vx  = vx;
    sp.vy  = vy;
    sp.vz  = vz;

    //printf("VELOCITY SETPOINT UVW = [ %.4f , %.4f , %.4f ] \n", sp.vx, sp.vy, sp.vz);

}

/*
 * Set target local ned acceleration
 *
 * Modifies a mavlink_set_position_target_local_ned_t struct with target AX AY AZ
 * accelerations in the Local NED frame, in meters per second squared.
 */
void
set_acceleration(float ax, float ay, float az, mavlink_set_position_target_local_ned_t &sp)
{

    // NOT IMPLEMENTED
    fprintf(stderr,"set_acceleration doesn't work yet \n");
    throw 1;


    sp.type_mask =
        MAVLINK_MSG_SET_POSITION_TARGET_LOCAL_NED_ACCELERATION &
        MAVLINK_MSG_SET_POSITION_TARGET_LOCAL_NED_VELOCITY     ;

    sp.coordinate_frame = MAV_FRAME_LOCAL_NED;

    sp.afx  = ax;
    sp.afy  = ay;
    sp.afz  = az;
}

// the next two need to be called after one of the above

/*
 * Set target local ned yaw
 *
 * Modifies a mavlink_set_position_target_local_ned_t struct with a target yaw
 * in the Local NED frame, in radians.
 */
void
set_yaw(float yaw, mavlink_set_position_target_local_ned_t &sp)
{
    sp.type_mask &=
        MAVLINK_MSG_SET_POSITION_TARGET_LOCAL_NED_YAW_ANGLE ;

    sp.yaw  = yaw;

    printf("POSITION SETPOINT YAW = %.4f \n", sp.yaw);

}

/*
 * Set target local ned yaw rate
 *
 * Modifies a mavlink_set_position_target_local_ned_t struct with a target yaw rate
 * in the Local NED frame, in radians per second.
 */
void
set_yaw_rate(float yaw_rate, mavlink_set_position_target_local_ned_t &sp)
{
    sp.type_mask &=
        MAVLINK_MSG_SET_POSITION_TARGET_LOCAL_NED_YAW_RATE ;

    sp.yaw_rate  = yaw_rate;
}


// ----------------------------------------------------------------------------------
//   Autopilot Interface Class
// ----------------------------------------------------------------------------------

// ------------------------------------------------------------------------------
//   Con/De structors
// ------------------------------------------------------------------------------
Autopilot_Interface::
Autopilot_Interface(Serial_Port *serial_port_)
{
    // initialize attributes
    write_count = 0;

    reading_status = 0;      // whether the read thread is running
    writing_status = 0;      // whether the write thread is running
    control_status = 0;      // whether the autopilot is in offboard control mode
    setpoint_send_status = 0;      // whether the autopilot has recieved the  setpoint
    time_to_exit   = false;  // flag to signal thread exit

    read_tid  = 0; // read thread id
    write_tid = 0; // write thread id

    system_id    = 0; // system id
    autopilot_id = 0; // autopilot component id
    companion_id = 0; // companion computer component id

    current_messages.sysid  = system_id;
    current_messages.compid = autopilot_id;

    serial_port = serial_port_; // serial port management object

}

Autopilot_Interface::
~Autopilot_Interface()
{}


// ------------------------------------------------------------------------------
//   Update Setpoint
// ------------------------------------------------------------------------------
void
Autopilot_Interface::
update_setpoint(mavlink_set_position_target_local_ned_t setpoint)
{
    current_setpoint = setpoint;

    set_setpoint_sendstatus(true);
}

char
Autopilot_Interface::
get_setpoint_sendstatus()
{
    return setpoint_send_status;
}

void
Autopilot_Interface::
set_setpoint_sendstatus(char status)
{
    setpoint_send_status = status;
}




// ------------------------------------------------------------------------------
//  Check Vehicle's Armed State
// ------------------------------------------------------------------------------
bool
Autopilot_Interface::
is_armed()
{
    Mavlink_Messages messages = current_messages;

    uint8_t arm_state;
    mavlink_heartbeat_t heartbeat;

    heartbeat = messages.heartbeat;
    arm_state = heartbeat.system_status;

    // printf("arm_state=%d\n", arm_state);

    if(MAV_STATE_ACTIVE == arm_state) 
        return true;
    else
        return false;
}

// ------------------------------------------------------------------------------
//  Check Vehicle's Offboard mode state 
// ------------------------------------------------------------------------------
bool
Autopilot_Interface::
is_in_offboard_mode()
{
    Mavlink_Messages messages = current_messages;

    union px4_custom_mode custom_mode;
    uint32_t mode;
    mavlink_heartbeat_t heartbeat;

    heartbeat = messages.heartbeat;
    mode = heartbeat.custom_mode;
    custom_mode = *(px4_custom_mode*)(&mode);

    printf("Check OFFBOARD MODE, %d\n", custom_mode.main_mode);

    if (custom_mode.main_mode == PX4_CUSTOM_MAIN_MODE_OFFBOARD)
        return true;
    else 
        return false;

}

// ------------------------------------------------------------------------------
//   Read Messages
// ------------------------------------------------------------------------------
void
Autopilot_Interface::
read_messages()
{
    bool success;               // receive success flag
    bool received_all = false;  // receive only one message
    Time_Stamps this_timestamps;

    // Blocking wait for new data
    while ( !received_all and !time_to_exit )
    {
        // ----------------------------------------------------------------------
        //   READ MESSAGE
        // ----------------------------------------------------------------------
        mavlink_message_t message;
        success = serial_port->read_message(message);

        // ----------------------------------------------------------------------
        //   HANDLE MESSAGE
        // ----------------------------------------------------------------------
        if( success )
        {

            // Store message sysid and compid.
            // Note this doesn't handle multiple message sources.
            current_messages.sysid  = message.sysid;
            current_messages.compid = message.compid;

            // Handle Message ID
            switch (message.msgid)
            {

                case MAVLINK_MSG_ID_HEARTBEAT:
                {
                    //printf("MAVLINK_MSG_ID_HEARTBEAT\n");
                    mavlink_msg_heartbeat_decode(&message, &(current_messages.heartbeat));
                    current_messages.time_stamps.heartbeat = get_time_usec();
                    this_timestamps.heartbeat = current_messages.time_stamps.heartbeat;
                    break;
                }

                case MAVLINK_MSG_ID_SYS_STATUS:
                {
                    //printf("MAVLINK_MSG_ID_SYS_STATUS\n");
                    mavlink_msg_sys_status_decode(&message, &(current_messages.sys_status));
                    current_messages.time_stamps.sys_status = get_time_usec();
                    this_timestamps.sys_status = current_messages.time_stamps.sys_status;
                    break;
                }

                case MAVLINK_MSG_ID_BATTERY_STATUS:
                {
                    //printf("MAVLINK_MSG_ID_BATTERY_STATUS\n");
                    mavlink_msg_battery_status_decode(&message, &(current_messages.battery_status));
                    current_messages.time_stamps.battery_status = get_time_usec();
                    this_timestamps.battery_status = current_messages.time_stamps.battery_status;
                    break;
                }

                case MAVLINK_MSG_ID_RADIO_STATUS:
                {
                    //printf("MAVLINK_MSG_ID_RADIO_STATUS\n");
                    mavlink_msg_radio_status_decode(&message, &(current_messages.radio_status));
                    current_messages.time_stamps.radio_status = get_time_usec();
                    this_timestamps.radio_status = current_messages.time_stamps.radio_status;
                    break;
                }

                case MAVLINK_MSG_ID_LOCAL_POSITION_NED:
                {
                    // printf("MAVLINK_MSG_ID_LOCAL_POSITION_NED\n");
                    mavlink_msg_local_position_ned_decode(&message, &(current_messages.local_position_ned));
                    current_messages.time_stamps.local_position_ned = get_time_usec();
                    // printf("% .4f,% .4f,% .4f\n",  current_messages.local_position_ned.x, current_messages.local_position_ned.y, current_messages.local_position_ned.z);
                    this_timestamps.local_position_ned = current_messages.time_stamps.local_position_ned;
                    break;
                }

                case MAVLINK_MSG_ID_GLOBAL_POSITION_INT:
                {
                    //printf("MAVLINK_MSG_ID_GLOBAL_POSITION_INT\n");
                    mavlink_msg_global_position_int_decode(&message, &(current_messages.global_position_int));
                    current_messages.time_stamps.global_position_int = get_time_usec();
                    this_timestamps.global_position_int = current_messages.time_stamps.global_position_int;
                    break;
                }

                case MAVLINK_MSG_ID_POSITION_TARGET_LOCAL_NED:
                {
                    //printf("MAVLINK_MSG_ID_POSITION_TARGET_LOCAL_NED\n");
                    mavlink_msg_position_target_local_ned_decode(&message, &(current_messages.position_target_local_ned));
                    current_messages.time_stamps.position_target_local_ned = get_time_usec();
                    this_timestamps.position_target_local_ned = current_messages.time_stamps.position_target_local_ned;
                    break;
                }

                case MAVLINK_MSG_ID_POSITION_TARGET_GLOBAL_INT:
                {
                    //printf("MAVLINK_MSG_ID_POSITION_TARGET_GLOBAL_INT\n");
                    mavlink_msg_position_target_global_int_decode(&message, &(current_messages.position_target_global_int));
                    current_messages.time_stamps.position_target_global_int = get_time_usec();
                    this_timestamps.position_target_global_int = current_messages.time_stamps.position_target_global_int;
                    break;
                }

                case MAVLINK_MSG_ID_HIGHRES_IMU:
                {
                    //printf("MAVLINK_MSG_ID_HIGHRES_IMU\n");
                    mavlink_msg_highres_imu_decode(&message, &(current_messages.highres_imu));
                    current_messages.time_stamps.highres_imu = get_time_usec();
                    this_timestamps.highres_imu = current_messages.time_stamps.highres_imu;
                    break;
                }

                case MAVLINK_MSG_ID_ATTITUDE:
                {
                    //printf("MAVLINK_MSG_ID_ATTITUDE\n");
                    mavlink_msg_attitude_decode(&message, &(current_messages.attitude));
                    current_messages.time_stamps.attitude = get_time_usec();
                    this_timestamps.attitude = current_messages.time_stamps.attitude;
                    break;
                }

                case MAVLINK_MSG_ID_VFR_HUD:
                {
                    //printf("MAVLINK_MSG_ID_VFR_HUD, alt=%6.2f\n", current_messages.vfr_hud.alt);
                    mavlink_msg_vfr_hud_decode(&message, &(current_messages.vfr_hud));
                    current_messages.time_stamps.vfr_hud = get_time_usec();
                    this_timestamps.vfr_hud = current_messages.time_stamps.vfr_hud;
                    break;
                }

                default:
                {
                    // printf("Warning, did not handle message id %i\n",message.msgid);
                    break;
                }


            } // end: switch msgid

        } // end: if read message

        // Check for receipt of all items
        received_all =
                this_timestamps.heartbeat                  &&
//              this_timestamps.battery_status             &&
//              this_timestamps.radio_status               &&
//              this_timestamps.local_position_ned         &&
//              this_timestamps.global_position_int        &&
//              this_timestamps.position_target_local_ned  &&
//              this_timestamps.position_target_global_int &&
//              this_timestamps.highres_imu                &&
//              this_timestamps.attitude                   &&
                this_timestamps.sys_status
                ;

        // give the write thread time to use the port
        if ( writing_status > false ) {
            usleep(100); // look for components of batches at 10kHz
        }

    } // end: while not received all

    return;
}

// ------------------------------------------------------------------------------
//   Write Message
// ------------------------------------------------------------------------------
int
Autopilot_Interface::
write_message(mavlink_message_t message)
{
    // do the write
    int len = serial_port->write_message(message);

    // book keep
    write_count++;

    // Done!
    return len;
}

// ------------------------------------------------------------------------------
//   Write Setpoint Message
// ------------------------------------------------------------------------------
void
Autopilot_Interface::
write_setpoint()
{
    // --------------------------------------------------------------------------
    //   PACK PAYLOAD
    // --------------------------------------------------------------------------

    // pull from position target
    mavlink_set_position_target_local_ned_t sp = current_setpoint;

    // double check some system parameters
    if ( not sp.time_boot_ms )
        sp.time_boot_ms = (uint32_t) (get_time_usec()/1000);
    sp.target_system    = system_id;
    sp.target_component = autopilot_id;


    // --------------------------------------------------------------------------
    //   ENCODE
    // --------------------------------------------------------------------------

    mavlink_message_t message;
    mavlink_msg_set_position_target_local_ned_encode(system_id, companion_id, &message, &sp);

    // --------------------------------------------------------------------------
    //   WRITE
    // --------------------------------------------------------------------------

    // do the write
    int len = write_message(message);

    // check the write
    if ( len <= 0 )
        fprintf(stderr,"WARNING: could not send POSITION_TARGET_LOCAL_NED \n");
    //  else
    //      printf("%lu POSITION_TARGET  = [ %f , %f , %f ] \n", write_count, position_target.x, position_target.y, position_target.z);

    return;
}

void
Autopilot_Interface::
write_set_att()
{

    mavlink_set_attitude_target_t att_sp/* = attitude_setpoint*/;

    // if ( not att_sp.time_boot_ms )
     att_sp.time_boot_ms = (uint32_t) (get_time_usec()/1000);
    
    att_sp.target_system    = system_id;
    att_sp.target_component = autopilot_id;
    att_sp.type_mask = MAVLINK_MSG_SET_ATTITUDE_TARGET_ATTITUDE;
    // att_sp.q[0] = 0.707;
    // att_sp.q[1] = 0;
    // att_sp.q[2] = 0;
    // att_sp.q[3] = -0.707;
    // mavlink_euler_to_quaternion(float roll, float pitch, float yaw, float quaternion[4]);
    mavlink_euler_to_quaternion(0, 0, 1.571, &att_sp.q[0]);  /*1.571*/
    // mavlink_quaternion_to_euler(set_attitude_target.q,
    //                                     &_att_sp.roll_body, &_att_sp.pitch_body, &_att_sp.yaw_body);

    mavlink_message_t message;
    mavlink_msg_set_attitude_target_encode(system_id,  companion_id, &message, &att_sp);

     // do the write
    int len = write_message(message);

    // check the write
    if ( len <= 0 )
        fprintf(stderr,"WARNING: could not send POSITION_TARGET_LOCAL_NED \n");
}


// ------------------------------------------------------------------------------
//   Start Off-Board Mode
// ------------------------------------------------------------------------------
void
Autopilot_Interface::
enable_offboard_control()
{
    int enable_trytimes = 50;
    
    // Should only send this command once
    if ( control_status == false )
    {
        printf("Enable Offboaed Mode...\n");

        //   Toggle Offboard Mode
        int success = -1;

        // Sends the command to go off-board
        while(enable_trytimes--){
            
            success = toggle_offboard_control( true );
            if(success < 0){

                printf("Offboard Command Send failed!\n");
                throw EXIT_FAILURE;
            }

            usleep(400000);

            // success = toggle_offboard_control( true );
            // if(success < 0){

            //     printf("Offboard Command Send failed!\n");
            //     throw EXIT_FAILURE;
            // }
             // usleep(300000);

            if (is_in_offboard_mode()){
                control_status = true;   /* In offboard mode*/
                break;
            }

        }

        if(!control_status){
            
             printf("Enable offboard mode failed!\n");
             throw EXIT_FAILURE;
        }

        printf("\n");

    } // end: if not offboard_status

}


// ------------------------------------------------------------------------------
//   Stop Off-Board Mode
// ------------------------------------------------------------------------------
void
Autopilot_Interface::
disable_offboard_control()
{

    // Should only send this command once
    if ( control_status == true )
    {
        printf("DISABLE OFFBOARD MODE\n");

        // ----------------------------------------------------------------------
        //   TOGGLE OFF-BOARD MODE
        // ----------------------------------------------------------------------

        // Sends the command to stop off-board
        int success = toggle_offboard_control( false );

        // Check the command was written
        if ( success )
            control_status = false;
        else
        {
            fprintf(stderr,"Error: off-board mode not set, could not write message\n");
            //throw EXIT_FAILURE;
        }

        printf("\n");

    } // end: if offboard_status

}

// ------------------------------------------------------------------------------
//   Armed
// ------------------------------------------------------------------------------
void
Autopilot_Interface::
vehicle_armed()
{
    // Should only send this command once
    printf("Switch Vehicle to Armed...\n");

    // Sends the command to armed
    int success  = -1, armed_trytimes = 50;
    bool armed = false;
    
    while(armed_trytimes--){

        if (is_armed()){
            armed = true;
            break;
        }

        success = toggle_arm_disarm( true );
         if(success < 0){

                printf("Armed Command Send failed!\n");
                throw EXIT_FAILURE;
        }

        usleep(200000);

    }

    if(!armed){
        
        printf("Armed failed!\n");
        throw EXIT_FAILURE;
    }


    // Check the command was written
    if ( !success ) {
        fprintf(stderr,"Error: armed failed, could not write message\n");
    }

    printf("\n");


}

// ------------------------------------------------------------------------------
//   Disarm
// ------------------------------------------------------------------------------
void
Autopilot_Interface::
vehicle_disarm()
{
    // Should only send this command once
    printf("DISARM MODE\n");

    // Sends the command to go off-board
    int success = toggle_arm_disarm( false );

    // Check the command was written
    if ( !success ) {
        fprintf(stderr,"Error: disarm failed could not write message\n");
    }

    printf("\n");
}



// ------------------------------------------------------------------------------
//   Toggle Off-Board Mode
// ------------------------------------------------------------------------------
int
Autopilot_Interface::
toggle_offboard_control( bool flag )
{
    // Prepare command for off-board mode
    mavlink_command_long_t com = { 0 };
    com.target_system    = system_id;
    com.target_component = autopilot_id;
    com.command          = MAV_CMD_NAV_GUIDED_ENABLE;
    com.confirmation     = true;
    com.param1           = (float) flag; // flag >0.5 => start, <0.5 => stop

    // Encode
    mavlink_message_t message;
    mavlink_msg_command_long_encode(system_id, companion_id, &message, &com);

    // Send the message
    int len = serial_port->write_message(message);

    // Done!
    return len;
}

int
Autopilot_Interface::
toggle_land_control( bool flag )
{
    // Prepare command for off-board mode
    mavlink_command_long_t com = { 0 };
    com.target_system    = system_id;
    com.target_component = autopilot_id;
    com.command          = MAV_CMD_NAV_LAND;
    com.confirmation     = true;
    com.param1           = (float) flag; // flag >0.5 => start, <0.5 => stop

    // Encode
    mavlink_message_t message;
    mavlink_msg_command_long_encode(system_id, companion_id, &message, &com);

    // Send the message
    int len = serial_port->write_message(message);

    // Done!
    return len;
}

int
Autopilot_Interface::
toggle_return_control( bool flag )
{
    // Prepare command for off-board mode
    mavlink_command_long_t com = { 0 };
    com.target_system    = system_id;
    com.target_component = autopilot_id;
    com.command          = MAV_CMD_NAV_RETURN_TO_LAUNCH;
    com.confirmation     = true;
    com.param1           = (float) flag; // flag >0.5 => start, <0.5 => stop

    // Encode
    mavlink_message_t message;
    mavlink_msg_command_long_encode(system_id, companion_id, &message, &com);

    // Send the message
    int len = serial_port->write_message(message);

    // Done!
    return len;
}

// ------------------------------------------------------------------------------
//   Toggle arm-disarm Mode
// ------------------------------------------------------------------------------
int
Autopilot_Interface::
toggle_arm_disarm( bool flag )
{
    // Prepare command for off-board mode
    mavlink_command_long_t com = { 0 };
    com.target_system    = system_id;
    com.target_component = autopilot_id;
    com.command          = MAV_CMD_COMPONENT_ARM_DISARM;
    com.confirmation     = true;
    com.param1           = (float) flag; // flag >0.5 => start, <0.5 => stop

    // Encode
    mavlink_message_t message;
    mavlink_msg_command_long_encode(system_id, companion_id, &message, &com);

    // Send the message
    int len = serial_port->write_message(message);

    // Done!
    return len;
}

// ------------------------------------------------------------------------------
//   STARTUP
// ------------------------------------------------------------------------------
void
Autopilot_Interface::
start()
{
    int result;

    // --------------------------------------------------------------------------
    //   CHECK SERIAL PORT
    // --------------------------------------------------------------------------

    if ( serial_port->status != 1 ) // SERIAL_PORT_OPEN
    {
        fprintf(stderr,"ERROR: serial port not open\n");
        throw 1;
    }


    // --------------------------------------------------------------------------
    //   READ THREAD
    // --------------------------------------------------------------------------

    printf("START READ THREAD \n");

    result = pthread_create( &read_tid, NULL, &start_autopilot_interface_read_thread, this );
    if ( result ) throw result;

    // now we're reading messages
    printf("\n");


    // --------------------------------------------------------------------------
    //   CHECK FOR MESSAGES
    // --------------------------------------------------------------------------

    printf("CHECK FOR MESSAGES\n");

    while ( not current_messages.sysid )
    {
        if ( time_to_exit )
            return;
        usleep(500000); // check at 2Hz
    }

    printf("Found\n");

    // now we know autopilot is sending messages
    printf("\n");


    // --------------------------------------------------------------------------
    //   GET SYSTEM and COMPONENT IDs
    // --------------------------------------------------------------------------

    // This comes from the heartbeat, which in theory should only come from
    // the autopilot we're directly connected to it.  If there is more than one
    // vehicle then we can't expect to discover id's like this.
    // In which case set the id's manually.

    // System ID
    if ( not system_id )
    {
        system_id = current_messages.sysid;
        printf("GOT VEHICLE SYSTEM ID: %i\n", system_id );
    }

    // Component ID
    if ( not autopilot_id )
    {
        autopilot_id = current_messages.compid;
        printf("GOT AUTOPILOT COMPONENT ID: %i\n", autopilot_id);
        printf("\n");
    }


    // --------------------------------------------------------------------------
    //   GET INITIAL POSITION
    // --------------------------------------------------------------------------

    // Wait for initial position ned
    while ( not ( current_messages.time_stamps.local_position_ned &&
                  current_messages.time_stamps.attitude            )  )
    {
        if ( time_to_exit )
            return;
        usleep(500000);
    }

    // copy initial position ned
    Mavlink_Messages local_data = current_messages;
    initial_position.x        = local_data.local_position_ned.x;
    initial_position.y        = local_data.local_position_ned.y;
    initial_position.z        = local_data.local_position_ned.z;
    initial_position.vx       = local_data.local_position_ned.vx;
    initial_position.vy       = local_data.local_position_ned.vy;
    initial_position.vz       = local_data.local_position_ned.vz;
    initial_position.yaw      = local_data.attitude.yaw;
    initial_position.yaw_rate = local_data.attitude.yawspeed;

    printf("INITIAL POSITION XYZ = [ %.4f , %.4f , %.4f ] \n", initial_position.x, initial_position.y, initial_position.z);
    printf("INITIAL POSITION YAW = %.4f \n", initial_position.yaw);
    printf("\n");

    // we need this before starting the write thread


    // --------------------------------------------------------------------------
    //   WRITE THREAD
    // --------------------------------------------------------------------------
    printf("START WRITE THREAD \n");

    result = pthread_create( &write_tid, NULL, &start_autopilot_interface_write_thread, this );
    if ( result ) throw result;

    // wait for it to be started
    while ( not writing_status )
        usleep(100000); // 10Hz

    // now we're streaming setpoint commands
    printf("\n");


    // Done!
    return;

}


// ------------------------------------------------------------------------------
//   SHUTDOWN
// ------------------------------------------------------------------------------
void
Autopilot_Interface::
stop()
{
    // --------------------------------------------------------------------------
    //   CLOSE THREADS
    // --------------------------------------------------------------------------
    printf("CLOSE THREADS\n");

    // signal exit
    time_to_exit = true;

    // wait for exit
    pthread_join(read_tid ,NULL);
    pthread_join(write_tid,NULL);

    // now the read and write threads are closed
    printf("\n");

    // still need to close the serial_port separately
}

// ------------------------------------------------------------------------------
//   Read Thread
// ------------------------------------------------------------------------------
void
Autopilot_Interface::
start_read_thread()
{

    if ( reading_status != 0 )
    {
        fprintf(stderr,"read thread already running\n");
        return;
    }
    else
    {
        read_thread();
        return;
    }

}


// ------------------------------------------------------------------------------
//   Write Thread
// ------------------------------------------------------------------------------
void
Autopilot_Interface::
start_write_thread(void)
{
    if ( not writing_status == false )
    {
        fprintf(stderr,"write thread already running\n");
        return;
    }

    else
    {
        write_thread();
        return;
    }

}


// ------------------------------------------------------------------------------
//   Quit Handler
// ------------------------------------------------------------------------------
void
Autopilot_Interface::
handle_quit( int sig )
{

    disable_offboard_control();

    try {
        stop();

    }
    catch (int error) {
        fprintf(stderr,"Warning, could not stop autopilot interface\n");
    }

}



// ------------------------------------------------------------------------------
//   Read Thread
// ------------------------------------------------------------------------------
void
Autopilot_Interface::
read_thread()
{
    reading_status = true;

    while ( ! time_to_exit )
    {
        read_messages();
        usleep(100000); // Read batches at 10Hz
    }

    reading_status = false;

    return;
}


// ------------------------------------------------------------------------------
//   Write Thread
// ------------------------------------------------------------------------------
void
Autopilot_Interface::
write_thread(void)
{
    // signal startup
    writing_status = 2;

    // prepare an initial setpoint, just stay put
    mavlink_set_position_target_local_ned_t sp;
    sp.type_mask = MAVLINK_MSG_SET_POSITION_TARGET_LOCAL_NED_VELOCITY &
                   MAVLINK_MSG_SET_POSITION_TARGET_LOCAL_NED_YAW_RATE;
    sp.coordinate_frame = MAV_FRAME_LOCAL_NED;
    sp.vx       = 0.0;
    sp.vy       = 0.0;
    sp.vz       = 0.0;
    sp.yaw_rate = 0.0;

    // set position target
    current_setpoint = sp;

    // write a message and signal writing
    write_setpoint();
    writing_status = true;

    // Pixhawk needs to see off-board commands at minimum 2Hz,
    // otherwise it will go into fail safe
    int cnt = 0;
    while ( !time_to_exit )
    {
        write_setpoint();
        usleep(200000);   // Stream at 10Hz, need to > 2Hz
        
        // cnt++;
        // if(cnt % 5 == 0){
        //     printf("set att...\n");
        //     write_set_att();
        // }
    }

    // signal end
    writing_status = false;

    return;

}

// End Autopilot_Interface


// ------------------------------------------------------------------------------
//  Pthread Starter Helper Functions
// ------------------------------------------------------------------------------

void*
start_autopilot_interface_read_thread(void *args)
{
    // takes an autopilot object argument
    Autopilot_Interface *autopilot_interface = (Autopilot_Interface *)args;

    // run the object's read thread
    autopilot_interface->start_read_thread();

    // done!
    return NULL;
}

void*
start_autopilot_interface_write_thread(void *args)
{
    // takes an autopilot object argument
    Autopilot_Interface *autopilot_interface = (Autopilot_Interface *)args;

    // run the object's read thread
    autopilot_interface->start_write_thread();

    // done!
    return NULL;
}



