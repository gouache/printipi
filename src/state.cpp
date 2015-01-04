/* The MIT License (MIT)
 *
 * Copyright (c) 2014 Colin Wallace
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "state.h"

#include <iostream>
#include <fstream> //for ifstream, ofstream
#include <thread>
#include <string>

#include "catch.hpp"
#include "compileflags.h"
#include "filesystem.h"
#include "gparse/com.h"
#include "platforms/auto/thisthreadsleep.h"
#include "common/logging.h"

//MACHINE_PATH is calculated in the Makefile and then passed as a define through the make system (ie gcc -DMACHINEPATH='"path"')
#include MACHINE_PATH


//This test must be contained in a class so as to have access to special functions in the State (if it ever needs that)
struct TestClass {
    TestClass() {
        GIVEN("A State with Driver, Filesystem & Com interfaces") {
            //setup code:
            std::ofstream inputFile;
            //disable buffering before opening
            inputFile.rdbuf()->pubsetbuf(0, 0);
            inputFile.open("PRINTIPI_TEST_INPUT", std::fstream::out | std::fstream::trunc);
            std::ifstream outputFile;
            outputFile.rdbuf()->pubsetbuf(0, 0);
            //must open with the ::out flag to automatically create the file if it doesn't exist
            outputFile.open("PRINTIPI_TEST_OUTPUT", std::fstream::out | std::fstream::in | std::fstream::trunc);

            machines::MACHINE driver;
            FileSystem fs("./");
            gparse::Com com("PRINTIPI_TEST_INPUT", "PRINTIPI_TEST_OUTPUT");
            State<machines::MACHINE> state(driver, fs, com, true);

            std::thread eventThread([&](){ 
                state.eventLoop(); 
            });

            //convenience function to read and wait for the next line from Printipi's output
            auto readLine = [&]() {
                std::string read;
                char curChar = 0;
                while (curChar != '\n') {
                    if (outputFile.readsome(&curChar, 1) && curChar != '\n') {
                        read += curChar;
                    } 
                }
                return read;
            };

            //@cmd g-code command to send to printer (a newline character will be appended)
            //@expect expected response
            auto sendCommand = [&](const std::string &cmd, const std::string &expect) {
                INFO("Sending command: '" + cmd + "'");
                inputFile << cmd << '\n';
                INFO("It should be acknowledged with something that begins with '" + expect + "'");
                std::string got = readLine();
                REQUIRE(got.substr(0, expect.length()) == expect);
            };

            //Verify that the position as reported by the motion planner is near (@x, @y, @z)
            auto verifyPosition = [&](float x, float y, float z) {
            	Vector4f actualPos = state.motionPlanner().actualCartesianPosition();
                INFO("Actual position: " + std::string(actualPos));
                REQUIRE(actualPos.xyz().distance(x, y, z) <= 4);
            };

            bool hasExited = false;
            auto exitOnce = [&]() {
                if (!hasExited) {
                    sendCommand("M0", "ok");
                    eventThread.join();
                    hasExited = true;
                }
            };

            //each WHEN/THEN case corresponds to a single test;
            //the above setup code and the teardown code further below are re-run for EVERY 'when' case.
            //This is also repeated recursively.

            LOG("state.cpp: BEGIN TEST\n");
            //test homing
            WHEN("The machine is homed") {
                sendCommand("G28", "ok");
            }
            //test G1 movement
            WHEN("The machine is homed & moved to (40, -10, 50)") {
                sendCommand("G28", "ok");
                sendCommand("G1 X40 Y-10 Z50", "ok");
                //test G1 movement
                THEN("The actual position should be near (40, -10, 50)") {
	                exitOnce(); //force the G1 code to complete
	                verifyPosition(40, -10, 50);
            	}
            	//test successive G1 movements
            	WHEN("The machine is moved to another absolute position afterward, (-30, 20, 80) at F=3000") {
            		sendCommand("G1 X-30 Y20 Z80 F3000", "ok");
            		THEN("The actual position should be near (-30, 20, 80)") {
	            		exitOnce(); //force the G1 code to complete
	            		verifyPosition(-30, 20, 80);
            		}
            	}
            	//test G91 (relative) movement
            	WHEN("The machine is moved a RELATIVE amount (-70, 30, 30) at F=3000") {
            		//put into relative movement mode
            		sendCommand("G91", "ok");
            		sendCommand("G1 X-70 Y30 Z30 F3000", "ok");
            		THEN("The actual position should be near (-30, 20, 80)") {
	            		exitOnce(); //force the G1 code to complete
	            		verifyPosition(-30, 20, 80);
            		}
            	}
            	//test comment parsing
            	WHEN("A movement command to (30, 10, 30) is coupled with a comment") {
            		sendCommand("G1 X30 Y10 Z30; HELLO, I am a comment!", "ok");
            		THEN("The actual position should be near (30, 10, 30)") {
	            		exitOnce(); //force the G1 code to complete
	            		verifyPosition(30, 10, 30);
            		}
            	}
            }
            //test automatic homing
            WHEN("The machine is moved to (40, -10, 50) before being homed") {
            	sendCommand("G1 X40 Y-10 Z50", "ok");
                THEN("The actual position should be near (40, -10, 50)") {
	                exitOnce(); //force the G1 code to complete
	                verifyPosition(40, -10, 50);
            	}
            }
            //test automatic homing using G0
            WHEN("The machine is moved to (40, -10, 50) before being homed, using G0 command") {
            	sendCommand("G0 X40 Y-10 Z50", "ok");
                THEN("The actual position should be near (40, -10, 50)") {
	                exitOnce(); //force the G0 code to complete
	                verifyPosition(40, -10, 50);
            	}
            }
            //test using inch coordinates
            WHEN("The machine is moved to (-1, 2, 1) in inches") {
            	//home machine
            	sendCommand("G28", "ok");
            	//put into inches mode
            	sendCommand("G20", "ok");
            	//move absolute
            	sendCommand("G1 X-1 Y2 Z1", "ok");
            	THEN("The actual position (in mm) should be near (-1, 2, 1)*25.4") {
            		exitOnce(); //force the G1 code to complete
            		verifyPosition(-1*25.4, 2*25.4, 1*25.4);
            	}
            }
            //test M18; let steppers move freely
            WHEN("The M18 command is sent to let the steppers move freely") {
            	sendCommand("M18", "ok");
            	//"then the machine shouldn't crash"
            }
            //test gcode printing from file
            WHEN("Commands are read from a file with M32") {
            	//home
            	sendCommand("G28", "ok");
            	//"initialize" the SD card
            	sendCommand("M21", "ok");
            	std::ofstream gfile("test-printipi-m32.gcode", std::fstream::out | std::fstream::trunc);
            	//test newlines / whitespace
            	gfile << "\n";
            	gfile << " \t \n";
            	//test comment & G90
            	gfile << "G90 \t ; comment \n";
            	gfile << "G1 X40 Y-10 Z50";
            	AND_WHEN("The file is terminated with a newline") {
	            	//test ending the file WITHOUT a newline
	            	gfile << "\n" << std::flush;
	            	//load & run the file
	            	sendCommand("M32 test-printipi-m32.gcode", "ok");
	            	THEN("The actual position should be near (40, -10, 50)") {
	            		//note: Printipi is able to monitor multiple file inputs simultaneously,
	            		// if we send it M0 immediately, it may not have read the G1 from the file, and so it will exit
	            		// there is no way to query the status of this file read, so we must just sleep & hope
	            		SleepT::sleep_for(std::chrono::seconds(1));
		                exitOnce(); //force the G0 code to complete
		                verifyPosition(40, -10, 50);
	            	}
	            }
	            AND_WHEN("The file does NOT end on an empty line") {
	            	//test ending the file WITHOUT a newline
	            	gfile << std::flush;
	            	//load & run the file
	            	sendCommand("M32 test-printipi-m32.gcode", "ok");
	            	THEN("The actual position should be near (40, -10, 50)") {
	            		//note: Printipi is able to monitor multiple file inputs simultaneously,
	            		// if we send it M0 immediately, it may not have read the G1 from the file, and so it will exit
	            		// there is no way to query the status of this file read, so we must just sleep & hope
	            		SleepT::sleep_for(std::chrono::seconds(1));
		                exitOnce(); //force the G0 code to complete
		                verifyPosition(40, -10, 50);
	            	}
	            }
                AND_WHEN("The file contains more commands after a M99 command") {
                    gfile << "\n";
                    gfile << "M99\n";
                    gfile << "G1 X0 Y0 Z50\n" << std::flush;
                    //load & run the file
                    sendCommand("M32 test-printipi-m32.gcode", "ok");
                    THEN("The no commands past M99 should be processed & the actual position should be near (40, -10, 50)") {
                        //note: Printipi is able to monitor multiple file inputs simultaneously,
                        // if we send it M0 immediately, it may not have read the G1 from the file, and so it will exit
                        // there is no way to query the status of this file read, so we must just sleep & hope
                        SleepT::sleep_for(std::chrono::seconds(1));
                        exitOnce(); //force the G0 code to complete
                        verifyPosition(40, -10, 50);
                    }
                }
            }
            //test M84; stop idle hold (same as M18)
            WHEN("The M84 command is sent to stop the idle hold") {
            	sendCommand("M84", "ok");
            	//"then the machine shouldn't crash"
            }
            WHEN("The M106 command is sent to activate fans") {
            	sendCommand("M106", "ok");
            	WHEN("The M107 command is sent to disactivate fans") {
            		sendCommand("M107", "ok");
            		//"then the machine shouldn't crash"
            	}
            }
            WHEN("The M106 command is sent to activate fans at a specific PWM between 0.0-1.0") {
            	sendCommand("M106 S0.7", "ok");
            	//"then the machine shouldn't crash"
            }
            WHEN("The M106 command is sent to activate fans at a specific PWM between 0-255") {
            	sendCommand("M106 S64", "ok");
            	//"then the machine shouldn't crash", and S64 should be interpreted as 64/255 duty cycle.
            }
            WHEN("The M115 command is sent to get firmware info") {
                sendCommand("M115", "ok");
            }
            WHEN("The M117 command is sent") {
            	sendCommand("M117 Hello, World!", "ok");
            	//"then the machine shouldn't crash"
            }
            WHEN("The M280 command is sent with servo index=0") {
                sendCommand("M280 P0 S40.5", "ok");
                //"then the machine shouldn't crash"
            }
            WHEN("The M280 command is sent with servo index=-1 (invalid)") {
                sendCommand("M280 P-1 S40.5", "ok");
                //"then the machine shouldn't crash"
            }
            //Teardown code:
            exitOnce();
        }
    }
};


SCENARIO("State will respond correctly to gcode commands", "[state]") {
    TestClass();
}