//////////////////////////////////////////////////////////
// ROSfalcon Driver. Publishes and subscribes to falconMsgs for Novint Falcon.
//
// Using LibniFalcon
// Steven Martin
// Based on barrow_mechanics example by Alistair Barrow

#include <iostream>
#include <string>
#include <cmath>
#include <ros/ros.h>
#include "ros_falcon//falconPos.h"
#include "ros_falcon/falconForces.h"
#include "ros_falcon/FourButtonDown.h"

#include "geometry_msgs/PointStamped.h"

#include "falcon/core/FalconDevice.h"
#include "falcon/firmware/FalconFirmwareNovintSDK.h"
#include "falcon/util/FalconCLIBase.h"
#include "falcon/util/FalconFirmwareBinaryNvent.h"
#include "falcon/grip/FalconGripFourButton.h"
#include "falcon/kinematic/stamper/StamperUtils.h"
#include "falcon/kinematic/FalconKinematicStamper.h"
#include "falcon/core/FalconGeometry.h"
#include "falcon/gmtl/gmtl.h"
#include "falcon/util/FalconFirmwareBinaryNvent.h"

using namespace libnifalcon;
using namespace std;
using namespace StamperKinematicImpl;
using namespace ros_falcon;
FalconDevice m_falconDevice;

/**********************************************
This function initialises the novint falcon controller

NoFalcons is the index of the falcon which you wish to initialise
Index 0 is first falcon.
**********************************************/

bool init_falcon(int NoFalcon)

{
	cout << "Falcon " << NoFalcon << ": Setting up LibUSB" << endl;
	m_falconDevice.setFalconFirmware<FalconFirmwareNovintSDK>(); // Set Firmware
	if (!m_falconDevice.open(NoFalcon))							 // Open falcon @ index 0  (index needed for multiple falcons, assuming only one connected)
	{
		cout << "Failed to find falcon " << NoFalcon << endl;
		return false;
	}
	else
	{
		cout << "Falcon " << NoFalcon << " Found" << endl;
	}

	// There's only one kind of firmware right now, so automatically set that.
	m_falconDevice.setFalconFirmware<FalconFirmwareNovintSDK>();
	// Next load the firmware to the device

	bool skip_checksum = false;
	// See if we have firmware
	bool firmware_loaded = false;
	firmware_loaded = m_falconDevice.isFirmwareLoaded();
	if (!firmware_loaded)
	{
		cout << "Falcon " << NoFalcon << ": Loading firmware" << endl;
		uint8_t *firmware_block;
		long firmware_size;
		{

			firmware_block = const_cast<uint8_t *>(NOVINT_FALCON_NVENT_FIRMWARE);
			firmware_size = NOVINT_FALCON_NVENT_FIRMWARE_SIZE;

			for (int i = 0; i < 20; ++i)
			{
				if (!m_falconDevice.getFalconFirmware()->loadFirmware(skip_checksum, NOVINT_FALCON_NVENT_FIRMWARE_SIZE, const_cast<uint8_t *>(NOVINT_FALCON_NVENT_FIRMWARE)))

				{
					cout << "Falcon " << NoFalcon << ": Firmware loading try failed" << endl;
				}
				else
				{
					firmware_loaded = true;
					break;
				}
			}
		}
	}
	else if (!firmware_loaded)
	{
		cout << "Falcon " << NoFalcon << ": No firmware loaded to device, and no firmware specified to load (--nvent_firmware, --test_firmware, etc...). Cannot continue" << endl;
		return false;
	}
	if (!firmware_loaded || !m_falconDevice.isFirmwareLoaded())
	{
		cout << "Falcon " << NoFalcon << ": No firmware loaded to device, cannot continue" << endl;
		return false;
	}
	cout << "Falcon " << NoFalcon << ": Firmware loaded" << endl;

	m_falconDevice.getFalconFirmware()->setHomingMode(true); // Set homing mode (keep track of encoders !needed!)
	cout << "Falcon " << NoFalcon << ": Homing Set" << endl;
	std::array<int, 3> forces = {0, 0, 0};
	m_falconDevice.getFalconFirmware()->setForces(forces);
	m_falconDevice.runIOLoop(); // read in data

	{
		bool stop = false;
		bool homing = false;
		bool homing_reset = false;
		usleep(100000);
		int tryLoad = 0;
		while (!stop) //&& tryLoad < 100
		{
			if (!m_falconDevice.runIOLoop())
				continue;

			if (!homing)
			{
				if (!m_falconDevice.getFalconFirmware()->isHomed())
				{
					m_falconDevice.getFalconFirmware()->setLEDStatus(libnifalcon::FalconFirmware::RED_LED);
					cout << "Falcon " << NoFalcon << " not currently homed. Move control all the way out then push straight all the way in." << endl;
				}

				homing = true;
			}

			if (homing && m_falconDevice.getFalconFirmware()->isHomed())
			{
				m_falconDevice.getFalconFirmware()->setLEDStatus(libnifalcon::FalconFirmware::BLUE_LED);
				cout << "Falcon " << NoFalcon << " homed" << endl;
				homing_reset = true;
				stop = true;
			}
			tryLoad++;
		}
		while (!m_falconDevice.runIOLoop())
			;
	}
	return true;
}

void forceCallback(const ros_falcon::falconForcesConstPtr &msg)
{
	std::array<double, 3> forces;
	forces[0] = msg->X;
	forces[1] = msg->Y;
	forces[2] = msg->Z;
	m_falconDevice.setForce(forces);

	cout << "set Force" << endl;
	// TODO Add safety to only apply forces for limited time
}

int main(int argc, char *argv[])
{
	ros::init(argc, argv, "ROSfalcon");
	int left_right = 0;

	if (argc > 1)
	{
		if (argv[1][1] == 'l')
			left_right = 1;
		else if (argv[1][1] == 'r')
			left_right = 0;
	}

	// TODO Driver currently assumes there is only one falcon attached
	if (init_falcon(left_right))
	{
		cout << "Falcon " << left_right << " Initialised Starting ROS Node" << endl;

		m_falconDevice.setFalconKinematic<libnifalcon::FalconKinematicStamper>();
		m_falconDevice.setFalconGrip<libnifalcon::FalconGripFourButton>();
		ros::NodeHandle node;

		// Start ROS Subscriber
		ros::Subscriber sub = node.subscribe("falconForce", 10, &forceCallback);

		// Start ROS Publisher
		ros::Publisher pub = node.advertise<geometry_msgs::PointStamped>("falconPos", 10);

		ros::Rate loop_rate(1000);

		ros::Publisher button_pub = node.advertise<ros_falcon::FourButtonDown>("falcon_button", 5);
		ros_falcon::FourButtonDown fbutton;

		unsigned int current_four_button = 0;

		while (node.ok())
		{
			if (m_falconDevice.runIOLoop())
			{
				//////////////////////////////////////////////
				// Request the current encoder positions:
				std::array<double, 3> Pos;
				Pos = m_falconDevice.getPosition();

				// Publish ROS values
				geometry_msgs::PointStamped position;

				position.header.frame_id = "map";

				position.point.x = Pos[0];
				position.point.y = Pos[1];
				position.point.z = Pos[2];
				pub.publish(position);

				unsigned int received_four_button = m_falconDevice.getFalconGrip()->getDigitalInputs();
				if (current_four_button != received_four_button)
				{
					fbutton.PLUS_BUTTON_DOWN = (bool)(received_four_button & FalconGripFourButton::PLUS_BUTTON);
					fbutton.FORWARD_BUTTON_DOWN = (bool)(received_four_button & FalconGripFourButton::FORWARD_BUTTON);
					fbutton.CENTER_BUTTON_DOWN = (bool)(received_four_button & FalconGripFourButton::CENTER_BUTTON);
					fbutton.MINUS_BUTTON_DOWN = (bool)(received_four_button & FalconGripFourButton::MINUS_BUTTON);

					button_pub.publish(fbutton);
					current_four_button = received_four_button;
				}
			}
			loop_rate.sleep();
		}
		m_falconDevice.close();
	}

	return 0;
}
