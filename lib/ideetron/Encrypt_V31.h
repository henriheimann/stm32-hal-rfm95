/******************************************************************************************
* Copyright 2015, 2016 Ideetron B.V.
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************************/
/******************************************************************************************
*
* File:        Encrypt_V31.h
* Author:      Gerben den Hartog
* Compagny:    Ideetron B.V.
* Website:     http://www.ideetron.nl/LoRa
* E-mail:      info@ideetron.nl
******************************************************************************************/

#ifndef ENCRYPT_V31_H
#define ENCRYPT_V31_H

void Calculate_MIC(unsigned char *Data, unsigned char *Final_MIC, unsigned char Data_Length, unsigned int Frame_Counter,
                   unsigned char Direction, unsigned char NwkSkey[16], unsigned char DevAddr[4]);

void Encrypt_Payload(unsigned char *Data, unsigned char Data_Length, unsigned int Frame_Counter,
                     unsigned char Direction, unsigned char Key[16], unsigned char DevAddr[4]);

void Generate_Keys(unsigned char *K1, unsigned char *K2, unsigned char NwkSkey[16]);

void Shift_Left(unsigned char *Data);

void XOR(unsigned char *New_Data,unsigned char *Old_Data);

#endif
