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
//	list of conditions and the following disclaimer.
//
// *  Redistributions in binary form must reproduce the above copyright notice,
//	this list of conditions and the following disclaimer in the documentation
//	and/or other materials provided with the distribution.
//
// *  Neither the name of EPRI nor the names of its contributors may
//	be used to endorse or promote products derived from this software without
//	specific prior written permission.
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

#include <chrono>
#include <iostream>
#include <iomanip>
#include <../CMSIS_RTOS/cmsis_os.h>
#include <cstring>
#include <timers.h>
#include <climits>

#include "STM32Debug.h"
#include "STM32Serial.h"
#include "CircularBuffer.h"

#include "main.h"
extern UART_HandleTypeDef huart6;

extern "C"
{
	static UART_HandleTypeDef		&g_Handle = huart6;
	static uint8_t					g_RXBuffer[256]{0};
	static CircularBuffer			g_Ring(g_RXBuffer, sizeof(g_RXBuffer));
	static EPRI::STM32SerialSocket*	g_pSocket = nullptr;
	static TimerHandle_t			g_hRXTimer = nullptr;
	static EPRI::ERROR_TYPE			g_LastError = EPRI::SUCCESSFUL;
	static size_t					g_BytesRead = 0;
	static osThreadId				g_CallbackThread = 0;

	// Himanshu
	UART_HandleTypeDef*				pg_Handle = &g_Handle;
	static uint8_t *				rg_RXBuffer = (uint8_t *)g_RXBuffer;
	uint8_t **						pg_RXBuffer = &rg_RXBuffer;
	CircularBuffer*					pg_Ring = &g_Ring;
	EPRI::STM32SerialSocket**		pg_pSocket = &g_pSocket;
	TimerHandle_t*					pg_hRXTimer = &g_hRXTimer;
	EPRI::ERROR_TYPE*				pg_LastError = &g_LastError;
	size_t*							pg_BytesRead = &g_BytesRead;

	static HAL_StatusTypeDef __UART_Receive_IT__(UART_HandleTypeDef * huart);

	void __USART6_IRQHandler__()	// HAL_UART_IRQHandler
	{
		UART_HandleTypeDef * huart = &g_Handle;

		uint32_t isrflags= READ_REG(huart->Instance->SR);
		uint32_t cr1its	 = READ_REG(huart->Instance->CR1);
		uint32_t cr3its  = READ_REG(huart->Instance->CR3);
		// uint32_t cr2its  = READ_REG(huart->Instance->CR2);
		// uint32_t errorflags = 0x00U;
		// uint32_t dmarequest = 0x00U;

		/* UART parity error interrupt occurred ------------------------------------*/
		if (((isrflags & USART_SR_PE) != RESET) && ((cr1its & USART_CR1_PEIE) != RESET))
		{
			__HAL_UART_CLEAR_PEFLAG(huart);
			huart->ErrorCode |= HAL_UART_ERROR_PE;
		}

		/* UART noise error interrupt occurred -------------------------------------*/
		if (((isrflags & USART_SR_NE) != RESET) && ((cr3its & USART_CR3_EIE) != RESET))
		{
			__HAL_UART_CLEAR_NEFLAG(huart);
			huart->ErrorCode |= HAL_UART_ERROR_NE;
		}

		/* UART frame error interrupt occurred -------------------------------------*/
		if (((isrflags & USART_SR_FE) != RESET) && ((cr3its & USART_CR3_EIE) != RESET))
		{
			__HAL_UART_CLEAR_FEFLAG(huart);
			huart->ErrorCode |= HAL_UART_ERROR_FE;
		}

		/* UART Over-Run interrupt occurred ----------------------------------------*/
		if (((isrflags & USART_SR_ORE) != RESET) && (((cr1its & USART_CR1_RXNEIE) != RESET) || ((cr3its & USART_CR3_EIE) != RESET)))
		{
			__HAL_UART_CLEAR_OREFLAG(huart);
			huart->ErrorCode |= HAL_UART_ERROR_ORE;
		}

		/* UART in mode Receiver ---------------------------------------------------*/
		if (((isrflags & USART_SR_RXNE) != RESET) && ((cr1its & USART_CR1_RXNEIE) != RESET))
		{
			__UART_Receive_IT__(huart);	// See below. // original in stm32f2xx_hal_uart.c > UART_Receive_IT
		}

		if (huart->ErrorCode != HAL_UART_ERROR_NONE)
		{
		  	/* Set the UART state ready to be able to start again the process */
			huart->RxState = HAL_UART_STATE_READY;

			HAL_UART_ErrorCallback(huart);
		}

//		/* UART in mode Transmitter ------------------------------------------------*/
//		if (((isrflags & USART_SR_TXE) != RESET) && ((cr1its & USART_CR1_TXEIE) != RESET))
//		{
//			UART_Transmit_IT(huart);
//			return;
//		}
//
//		/* UART in mode Transmitter end --------------------------------------------*/
//		if (((isrflags & USART_SR_TC) != RESET) && ((cr1its & USART_CR1_TCIE) != RESET))
//		{
//			UART_EndTransmit_IT(huart);
//			return;
//		}
	}

	static HAL_StatusTypeDef __UART_Receive_IT__(UART_HandleTypeDef * huart)
	{
		uint8_t    RXByte;		// pdata8bits in original (stm32f2xx_hal_uart.c)
		size_t     ActualBytes = 0;
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;

		xTimerStopFromISR(g_hRXTimer, &xHigherPriorityTaskWoken);
		if (xHigherPriorityTaskWoken)
		{
			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
		}

		// Himanshu
		if (huart->RxState == HAL_UART_STATE_BUSY_RX)
		{
			if (huart->Init.Parity == UART_PARITY_NONE)		// Wordlength is 8b only
			{
				RXByte = (huart->Instance->DR & (uint8_t)0x00FF);
			}
			else
			{
				RXByte = (huart->Instance->DR & (uint8_t)0x007F);
			}
			if(huart->pRxBuffPtr == g_RXBuffer)	// Himanshu - added if {condition} only
				g_Ring.Put(&RXByte, 1, &ActualBytes);
			else								// Himanshu - added else {block}
				*huart->pRxBuffPtr++ = RXByte;

			if (--huart->RxXferCount == 0)
			{
				/* Disable the UART Data Register not empty Interrupt */
				__HAL_UART_DISABLE_IT(huart, UART_IT_RXNE);

				/* Disable the UART Parity Error Interrupt */
				__HAL_UART_DISABLE_IT(huart, UART_IT_PE);

				/* Disable the UART Error Interrupt: (Frame error, noise error, overrun error) */
				__HAL_UART_DISABLE_IT(huart, UART_IT_ERR);

				/* Rx process is completed, restore huart->RxState to Ready */
				huart->RxState = HAL_UART_STATE_READY;

				/*Call Rx complete callback (when ReceptionType is not 'till IDLE' - see original > line 3601).*/
				HAL_UART_RxCpltCallback(huart);
			}
			return HAL_OK;
		}
		return HAL_BUSY;
	}

	void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
	{
		g_LastError = (huart->RxXferCount == 0 ? EPRI::SUCCESSFUL : !EPRI::SUCCESSFUL);
		g_BytesRead = huart->RxXferSize - huart->RxXferCount;	// Himanshu - added third {+} expression (+ not g_ClearInterrupt)
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xTaskNotifyFromISR(g_CallbackThread,
			0x00000001,
			eSetBits,
			&xHigherPriorityTaskWoken);
		if (xHigherPriorityTaskWoken)
		{
			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
		}
	}

	void vTimerCallback(TimerHandle_t xTimer)
	{
		UART_HandleTypeDef * huart = &g_Handle;

		/* Disable the UART Data Register not empty Interrupt */
		__HAL_UART_DISABLE_IT(huart, UART_IT_RXNE);

		/* Disable the UART Parity Error Interrupt */
		__HAL_UART_DISABLE_IT(huart, UART_IT_PE);

		/* Disable the UART Error Interrupt: (Frame error, noise error, overrun error) */
		__HAL_UART_DISABLE_IT(huart, UART_IT_ERR);

		/* Rx process is timed out, restore huart->RxState to Ready */
		huart->RxState = HAL_UART_STATE_READY;

		if (g_pSocket && g_pSocket->m_Read)
		{
			g_LastError = EPRI::ERR_TIMEOUT;
			g_BytesRead = huart->RxXferSize - huart->RxXferCount;
			BaseType_t xHigherPriorityTaskWoken = pdFALSE;
			xTaskNotify(g_CallbackThread,
				0x00000001,
				eSetBits);
			if (xHigherPriorityTaskWoken)
			{
				portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
			}
		}

		HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
	}

	void CallbackThread(void const * argument)
	{
		EPRI::STM32SerialSocket * pSocket = (EPRI::STM32SerialSocket *) argument;
		uint32_t                  NotifiedValue = 0;
		for (;;)
		{
			xTaskNotifyWait( 0x00,
				ULONG_MAX,
				&NotifiedValue,
				portMAX_DELAY);
			if (NotifiedValue & 0x00000001 && pSocket->m_Read)
			{
				pSocket->m_Read(g_LastError, g_BytesRead);
			}

			HAL_GPIO_TogglePin(LD3_GPIO_Port, LD3_Pin);
		}
	}
}

namespace EPRI
{
	//
	// STM32Serial
	//
	STM32Serial::STM32Serial()
	{
	}

	STM32Serial::~STM32Serial()
	{
	}

	//
	// STM32SerialSocket
	//
	STM32SerialSocket::STM32SerialSocket(const STM32Serial::Options& Opt)
		: m_Options(Opt)
	{
		if (g_hRXTimer)
		{
			xTimerDelete(g_hRXTimer, pdMS_TO_TICKS(100));
		}
		g_hRXTimer = xTimerCreate("RXTimer", pdMS_TO_TICKS(1000), pdFALSE, nullptr, vTimerCallback);

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#endif
		osThreadDef(Callback, CallbackThread, osPriorityNormal, 0, 20 * configMINIMAL_STACK_SIZE);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
		g_CallbackThread = osThreadCreate(osThread(Callback), this);

	}

	STM32SerialSocket::~STM32SerialSocket()
	{
		if (g_hRXTimer)
		{
			xTimerDelete(g_hRXTimer, pdMS_TO_TICKS(100));
		}
		g_hRXTimer = nullptr;
	}

	ERROR_TYPE STM32SerialSocket::Open(const char * DestinationAddress /*= nullptr*/, int Port /*= DEFAULT_WiFi_PORT*/)
	{
		ERROR_TYPE RetVal = SUCCESSFUL;
		SetPortOptions();
		if (HAL_UART_Init(&g_Handle) != HAL_OK)
		{
			RetVal = !SUCCESSFUL;
		}
		else
		{
			g_pSocket = this;
		}
		if (m_Connect)
		{
			if(DestinationAddress == nullptr)		// Himanshu - added if statement {only}
				m_Connect(RetVal);
		}
		return RetVal;
	}

//	STM32SerialSocket::ConnectCallbackFunction STM32SerialSocket::RegisterConnectHandler(ConnectCallbackFunction Callback)
//	{
//		ConnectCallbackFunction RetVal = m_Connect;
//		m_Connect = Callback;
//		return RetVal;
//	}

	STM32Serial::Options STM32SerialSocket::GetOptions()
	{
		return m_Options;
	}

	ERROR_TYPE STM32SerialSocket::Write(const WiFiBuffer& Data, bool Asynchronous /*= false*/)
	{
		ERROR_TYPE       RetVal = SUCCESSFUL;
//		Base()->GetDebug()->TRACE_VECTOR("SW", Data);

		if (Asynchronous)
		{
			// TODO
		}
		else
		{
			HAL_StatusTypeDef HALVal = HAL_UART_Transmit(&g_Handle, (uint8_t *) Data.GetData(), (uint16_t) Data.Size(), HAL_MAX_DELAY);
			if (HAL_OK != HALVal)
				RetVal = !SUCCESSFUL;
		}
		return RetVal;
	}

//	STM32SerialSocket::WriteCallbackFunction STM32SerialSocket::RegisterWriteHandler(WriteCallbackFunction Callback)
//	{
//		WriteCallbackFunction RetVal = m_Write;
//		m_Write = Callback;
//		return RetVal;
//	}

	ERROR_TYPE STM32SerialSocket::Read(WiFiBuffer * pData,
		size_t ReadAtLeast /*= 0*/,
		uint32_t TimeOutPeriodInMS /*= 0*/,
		size_t * pActualBytes /*= nullptr*/)
	{
		ERROR_TYPE        RetVal = SUCCESSFUL;
		HAL_StatusTypeDef HALVal;

		if (0 == ReadAtLeast)
			ReadAtLeast = 1;
		if (0 == TimeOutPeriodInMS)
			TimeOutPeriodInMS = HAL_MAX_DELAY;
		if (pData)
		{
			size_t BufferIndex = pData->AppendExtra(ReadAtLeast);
			HALVal = HAL_UART_Receive(&g_Handle, &(*pData)[BufferIndex], ReadAtLeast, TimeOutPeriodInMS);
			if (HAL_OK != HALVal)
				RetVal = !SUCCESSFUL;
			if(HAL_TIMEOUT == HALVal)		// Himanshu
				RetVal = ERR_TIMEOUT;
			if (pActualBytes)
				*pActualBytes = ReadAtLeast - g_Handle.RxXferCount;
		}
		else
		{
			if(g_hRXTimer)
			{
				const TickType_t TicksToWait = pdMS_TO_TICKS(5000);
				xTimerStop(g_hRXTimer, TicksToWait);
				if (TimeOutPeriodInMS)
				{
					xTimerChangePeriod(g_hRXTimer, pdMS_TO_TICKS(TimeOutPeriodInMS), TicksToWait);
					xTimerStart(g_hRXTimer, TicksToWait);
				}
			}
			g_LastError = EPRI::SUCCESSFUL;
			HALVal = HAL_UART_Receive_IT(&g_Handle, g_RXBuffer, ReadAtLeast);
//			HALVal = HAL_UARTEx_ReceiveToIdle_IT(&g_Handle, g_RXBuffer, UINT16_MAX);
			if (HAL_OK != HALVal)
			{
				RetVal = !SUCCESSFUL;
			}
		}

		return RetVal;
	}

	bool STM32SerialSocket::AppendAsyncReadResult(WiFiBuffer * pData, size_t ReadAtLeast /*= 0*/)
	{
		size_t RingCount;
		g_Ring.Count(&RingCount);

		if (0 == ReadAtLeast)
		{
			ReadAtLeast = RingCount;
		}
		if (ReadAtLeast < RingCount)
		{
			return false;
		}
		uint8_t * pBuffer = &(*pData)[pData->AppendExtra(ReadAtLeast)];
		return CircularBuffer::OK == g_Ring.Get(pBuffer, ReadAtLeast, &RingCount);
	}

	STM32SerialSocket::ReadCallbackFunction STM32SerialSocket::RegisterReadHandler(ReadCallbackFunction Callback)
	{
		ReadCallbackFunction RetVal = m_Read;
		m_Read = Callback;
		return RetVal;
	}

	ERROR_TYPE STM32SerialSocket::Close()		// Do not use m_Close with TCPWrapper - will cause recursive loop.
	{
		HAL_UART_DeInit(&g_Handle);
		std::memset(&g_Handle, '\0', sizeof(g_Handle));
		g_pSocket = nullptr;

		return SUCCESSFUL;
	}

//	STM32SerialSocket::CloseCallbackFunction STM32SerialSocket::RegisterCloseHandler(CloseCallbackFunction Callback)
//	{
//		CloseCallbackFunction RetVal = m_Close;
//		m_Close = Callback;
//		return RetVal;
//	}

	bool STM32SerialSocket::IsConnected()
	{
		return g_pSocket;
	}

	ERROR_TYPE STM32SerialSocket::Accept(const char * DestinationAddress /*= nullptr*/, int Port /*= DEFAULT_WiFi_PORT*/)	// Himanshu - status TESTING
	{
		// TODO - verify this operation (not working - see SerialWrapper::Serial_Close() )
		if(this->Open(DestinationAddress, Port) != SUCCESSFUL)
		{
			printf("Connection failed.\r\n");
			return !SUCCESSFUL;
		}
		printf("Connected.\r\n");
		return SUCCESSFUL;
	}

	ERROR_TYPE STM32SerialSocket::Flush(FlushDirection Direction)
	{
		if (Direction == FlushDirection::BOTH || FlushDirection::RECEIVE)
		{
			g_Ring.Clear();
		}
		return SUCCESSFUL;
	}

	ERROR_TYPE STM32SerialSocket::SetOptions(const STM32Serial::Options& Opt)
	{
		m_Options = Opt;
		return SUCCESSFUL;
	}

	void STM32SerialSocket::SetPortOptions()
	{
		const uint32_t BAUDS[] =
		{
			300,
			600,
			1200,
			1800,
			2400,
			4800,
			9600,
			19200,
			38400,
			57600,
			115200,
			230400,
			460800,
			500000,
			576000,
			921600,
			1000000,
			1152000,
			1500000,
			2000000,
			2500000,
			3000000,
			3500000,
			4000000
		};
		uint32_t PARITIES[] =
		{
			UART_PARITY_NONE,
			UART_PARITY_EVEN,
			UART_PARITY_ODD
		};
		uint32_t STOPBITS[] =
		{
			UART_STOPBITS_1,
			UART_STOPBITS_2,
			UART_STOPBITS_2
		};
		g_Handle.Instance        = USART6;
		g_Handle.Init.BaudRate   = BAUDS[m_Options.m_BaudRate];
		g_Handle.Init.WordLength = UART_WORDLENGTH_8B;
		g_Handle.Init.StopBits   = STOPBITS[m_Options.m_StopBits];
		g_Handle.Init.Parity     = PARITIES[m_Options.m_Parity];
		g_Handle.Init.Mode       = UART_MODE_TX_RX;
		g_Handle.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
		g_Handle.Init.OverSampling = UART_OVERSAMPLING_16;
	}

}
