/*
 *  SDMMDDebugger.c
 *  SDM_MD_Demo
 *
 *  Created by sam on 6/5/13.
 *  Copyright 2013 Sam Marshall. All rights reserved.
 *
 */

#ifndef _SDMMDDEBUGGER_C_
#define _SDMMDDEBUGGER_C_

#import "SDMMDDebugger.h"

static char *kHexEncode = "0123456789ABCDEF";
#define kHexDecode(byte) ((byte >= '0' && byte <= '9') ? (byte - '0') : ( (byte >= 'a' && byte <= 'f') ? (10 + byte - 'a') : ((byte >= 'A' && byte <= 'F') ? (10 + byte - 'A') : byte)))

uint32_t GenerateChecksumForData(char *strPtr, uint32_t length) {
	uint32_t checksum = 0;
	for (uint32_t i = 0; i < length; i++) {
		checksum += strPtr[i];
	}
	return checksum;
}

sdmmd_return_t SDMMDStartDebuggingSessionOnDevice(SDM_AMDeviceRef device, SDM_AMDebugConnectionRef *connection) {
	sdmmd_return_t result = AMDeviceConnect(device);
	if (SDM_MD_CallSuccessful(result)) {
		result = AMDeviceStartSession(device);
		if (SDM_MD_CallSuccessful(result)) {
			CFDictionaryRef dict = NULL;
			*connection = SDM_AMDServiceConnectionCreate(kCFAllocatorDefault, device, dict);
			uint32_t con = 0;
			result = AMDeviceStartService(device, CFSTR(AMSVC_DEBUG_SERVER), &con, NULL);
			(*connection)->body.socket = con;
		}
	}
	return result;
}

sdmmd_return_t SDMMDStopDebuggingSessionOnDevice(SDM_AMDeviceRef device, SDM_AMDebugConnectionRef *connection) {
	sdmmd_return_t result = MDERR_OK;
	if (SDM_MD_CallSuccessful(result)) {
		result = AMDeviceStopSession(device);
		if (SDM_MD_CallSuccessful(result)) {
			result = AMDeviceDisconnect(device);
		}
	}
	return result;
}

CFStringRef SDMMDEncodeForDebuggingCommand(CFStringRef command) {
	unsigned char *commandString = malloc(CFStringGetLength(command));
	uint32_t length = CFStringGetBytes(command, CFRangeMake(0,CFStringGetLength(command)), kCFStringEncodingUTF8, 0, true, commandString, CFStringGetLength(command), NULL);
	unsigned char *encodedCommand = malloc(sizeof(unsigned char));
	uint32_t pos = 0;
	for (uint32_t i = 0; i < length; i++) {
		pos = (i*(sizeof(char)*2));
		encodedCommand = realloc(encodedCommand, pos+(sizeof(unsigned char)*2));
		encodedCommand[pos] = kHexEncode[commandString[i] >> 4];
		encodedCommand[pos+1] = kHexEncode[commandString[i] & 0x0f];
	}
	return CFStringCreateWithBytesNoCopy(kCFAllocatorDefault, encodedCommand, length*2, kCFStringEncodingUTF8, true, kCFAllocatorDefault);
}

sdmmd_debug_return_t SDMMDDebuggingSend(SDM_AMDebugConnectionRef connection, SDMMDDebugCommandType commandType, CFStringRef encodedCommand) {
	CFDataRef data = NULL;
	char *commandData = malloc(sizeof(char));
	uint32_t pos = 0;
	commandData[pos++] = '$';
	for (uint32_t i = 0; i < strlen(commandType.code); i++) {
		commandData[pos++] = commandType.code[i];
		commandData = realloc(commandData, sizeof(char)*(pos+1));
	}
	if (encodedCommand) {
		unsigned char *commandString = malloc(CFStringGetLength(encodedCommand));
		uint32_t length = CFStringGetBytes(encodedCommand, CFRangeMake(0,CFStringGetLength(encodedCommand)), kCFStringEncodingUTF8, 0, true, commandString, CFStringGetLength(encodedCommand), NULL);
		for (uint32_t i = 0; i < length; i++) {
			commandData[pos++] = commandString[i];
			commandData = realloc(commandData, sizeof(char)*(pos+1));
		}
	}
	uint32_t checksum = GenerateChecksumForData(&commandData[1], pos-1);
	commandData = realloc(commandData, sizeof(char)*(pos+3));
	commandData[pos++] = '#';
	commandData[pos++] = kHexEncode[(checksum >> 4) & 0xf];
	commandData[pos++] = kHexEncode[checksum & 0xf];
	
	CFDataRef sending = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, (UInt8 *)commandData, pos+3, kCFAllocatorDefault);
	sdmmd_return_t result = SDMMDServiceSend(connection->body.socket, sending);
	if (SDM_MD_CallSuccessful(result)) {
		result = SDMMDDebuggingReceive(connection, &data).result;
	}
	CFRelease(sending);
	free(commandData);
	return (sdmmd_debug_return_t){result, data};
}

sdmmd_debug_return_t SDMMDDebuggingReceive(SDM_AMDebugConnectionRef connection, CFDataRef *data) {
	unsigned char ackBuffer[1];
	CFDataRef ackData = CFDataCreate(kCFAllocatorDefault, ackBuffer, 1);
	sdmmd_return_t result = SDMMDServiceReceive(connection->body.socket, &ackData);	
	if (CFDataGetBytePtr(ackData)[0] == '+') {
		unsigned char commandBuffer[1];
		CFDataRef commandData = CFDataCreate(kCFAllocatorDefault, commandBuffer, 1);
		result = SDMMDServiceReceive(connection->body.socket, &commandData);	
		if (CFDataGetBytePtr(commandData)[0] == '$') {
			char *response = malloc(sizeof(char));			
			uint32_t position = 0, checksumCount = 3;
			while (checksumCount) {
				CFDataRef responseData = CFDataCreate(kCFAllocatorDefault, (UInt8 *)&response[position], 1);
				result = SDMMDServiceReceive(connection->body.socket, &responseData);
				response[position] = CFDataGetBytePtr(responseData)[0];
				if (response[position] == '#' || checksumCount < 3) {
					checksumCount--;
				}
				position++;
				response = realloc(response, sizeof(char)*(position+1));
				CFRelease(responseData);
			}
			uint32_t checksum = GenerateChecksumForData(response, position-3);
			if (kHexDecode(response[position-2]) == ((checksum >> 4) & 0xf) && kHexDecode(response[position-1]) == (checksum & 0xf)) {
				*data = CFDataCreate(kCFAllocatorDefault, (UInt8 *)response, position-3);
			} else {
				result = MDERR_QUERY_FAILED;
			}
		}
	}
	return (sdmmd_debug_return_t){result, *data};
}

#endif