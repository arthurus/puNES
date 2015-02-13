/*
 * version.h
 *
 *  Created on: 08/set/2010
 *      Author: fhorse
 */

#ifndef VERSION_H_
#define VERSION_H_

#if !defined (RELEASE)
#include "build.h"
#endif

#define VER1        "0"
#define VER1_INT    0

#define VER2        "91"
#define VER2_INT    91

#define VERSION     VER1 "." VER2 " WIP"
#define NAME        "puNES"
#define AUTHOR      "FHorse"
#define COPYRIGTH   "(c) " AUTHOR " 2010 - 2015"
#define COMMENT     "Nintendo Entertainment System Emulator"
#define WEBSITE     "http://forums.nesdev.com/viewtopic.php?f=3&amp;t=6928"

#endif /* VERSION_H_ */