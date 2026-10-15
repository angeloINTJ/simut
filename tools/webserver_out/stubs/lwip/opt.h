/* Host stand-in: the one lwIP option HTTPServer.cpp reads. The firmware build
 * checks the same static_assert against the real lwipopts.h. */
#pragma once
#define TCP_MSS 1460
