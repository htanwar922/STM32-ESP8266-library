// ===========================================================================
// Copyright (c) 2018, Electric Power Research Institute (EPRI)
// All rights reserved.
//
// DLMS-COSEM ("this software") is licensed under BSD 3-Clause license.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
// *  Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// *  Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// *  Neither the name of EPRI nor the names of its contributors may
//    be used to endorse or promote products derived from this software without
//    specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
// NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
// OF SUCH DAMAGE.
//
// This EPRI software incorporates work covered by the following copyright and permission
// notices. You may not use these works except in compliance with their respective
// licenses, which are provided below.
//
// These works are provided by the copyright holders and contributors "as is" and any express or
// implied warranties, including, but not limited to, the implied warranties of merchantability
// and fitness for a particular purpose are disclaimed.
//
// This software relies on the following libraries and licenses:
//
// ###########################################################################
// Boost Software License, Version 1.0
// ###########################################################################
//
// * asio v1.10.8 (https://sourceforge.net/projects/asio/files/)
//
// Boost Software License - Version 1.0 - August 17th, 2003
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
//
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
// 
#include <cmsis_os.h>
#include "main.h"
#include "string.h"

#include "WiFiBuffer.h"
#include "STM32-Server.h"
#include "STM32Debug.h"
#include "STM32TCP.h"
#include "ESP8266_WiFi.h"

using namespace std;
using namespace EPRI;

extern WiFiBuffer wifiRxBuffer;
extern UART_HandleTypeDef huart6;
extern "C" uint8_t ** pg_RXBuffer;

WiFiDevice * wifi;
STM32TCPSocket *pSocket;
STM32Base * g_pBase;		// must be defined for STM32Debug to work

void Socket_Read_Handler(ERROR_TYPE Error, size_t BytesReceived);

void RunServer()
{
	g_pBase = new STM32Base();

	wifi = new ESP8266Device(WiFi_GPIO_Pin(WIFI_RST_GPIO_Port, WIFI_RST_Pin));
	wifi->WIFI_DEBUG_LVL = WiFiDevice::LVL_LOW;

	pSocket = new STM32TCPSocket(
			STM32Serial::Options(STM32Serial::Options::BaudRate::BAUD_115200),
			STM32TCP::Options(STM32TCP::Options::MODE_SERVER),
			wifi
		);

	pSocket->RegisterReadHandler(Socket_Read_Handler);

	if(SUCCESSFUL != pSocket->Open(nullptr, STM32SerialSocket::DEFAULT_WiFi_PORT, "Xeon", "Himanshu"))
		printf("Socket opening unsuccessful.\r\n");
	else
	{
		wifiRxBuffer.Clear();
		pSocket->Read(nullptr, 1U);
	}
	printf("OKAY\r\n");
	for(;;)
	{
		HAL_GPIO_TogglePin(LD1_GPIO_Port, LD1_Pin);
		osDelay(2000);
	}
}

void Socket_Read_Handler(ERROR_TYPE Error, size_t BytesReceived)
{
	if (SUCCESSFUL == Error || BytesReceived)
	{
		pSocket->AppendAsyncReadResult(&wifiRxBuffer, BytesReceived);
		size_t ActualBytes = 0, TotalBytes = 0;
		do {
			ActualBytes = wifi->Read(1000, WIFI_RX_BUFFER_LEN, false);
			TotalBytes += ActualBytes;
		} while(ActualBytes != 0 and ActualBytes == WIFI_RX_BUFFER_LEN);
		Base()->GetDebug()->TRACE_VECTOR("SR", wifiRxBuffer);
		printf("Received : %s\r\n===\r\n", (const char *)wifiRxBuffer.GetData());
		wifiRxBuffer.Clear();
		pSocket->Read(nullptr, 1U);
	}
}
