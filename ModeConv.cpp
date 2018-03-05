/*
 *   Copyright (C) 2010,2014,2016 by Jonathan Naylor G4KLX
 *   Copyright (C) 2016 Mathias Weyland, HB9FRV
 *   Copyright (C) 2018 by Andy Uribe CA6JAU
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "ModeConv.h"
#include "NXDNDefines.h"
#include "Utils.h"

#include "Log.h"

#include <cstdio>
#include <cassert>

const unsigned char AMBE_SILENCE[] = {0xB9U, 0xE8U, 0x81U, 0x52U, 0x61U, 0x73U, 0x00U, 0x2AU, 0x6BU};

CModeConv::CModeConv() :
m_nxdnN(0U),
m_dmrN(0U),
m_NXDN(5000U, "DMR2NXDN"),
m_DMR(5000U, "NXDN2DMR")
{
}

CModeConv::~CModeConv()
{
}

void CModeConv::putDMR(unsigned char* data)
{
	unsigned char v_ambe[9U];

	assert(data != NULL);

	m_NXDN.addData(&TAG_DATA, 1U);
	m_NXDN.addData(data, 9U);
	//CUtils::dump(1U, "NXDN Voice:", data, 9U);
	m_nxdnN += 1U;
	
	data += 9U;
	for (unsigned int i = 0U; i < 4U; i++)
		v_ambe[i] = data[i];
	
	v_ambe[4U] = data[4U] & 0xF0;
	v_ambe[4U] |= data[10U] & 0x0F;
	
	for (unsigned int i = 0U; i < 4U; i++)
		v_ambe[i + 5U] = data[i + 11U];

	m_NXDN.addData(&TAG_DATA, 1U);
	m_NXDN.addData(v_ambe, 9U);
	//CUtils::dump(1U, "NXDN Voice:", v_ambe, 9U);
	m_nxdnN += 1U;

	data += 15U;;
	m_NXDN.addData(&TAG_DATA, 1U);
	m_NXDN.addData(data, 9U);
	//CUtils::dump(1U, "NXDN Voice:", data, 9U);
	m_nxdnN += 1U;
}

void CModeConv::putNXDN(unsigned char* data)
{
	assert(data != NULL);

	data += NXDN_FSW_LICH_SACCH_LENGTH_BYTES;

	for (unsigned int i = 0U; i < 4U; i++) {
		data += (9U * i);

		m_DMR.addData(&TAG_DATA, 1U);
		m_DMR.addData(data, 9U);

		//CUtils::dump(1U, "DMR Voice:", data, 9U);

		m_dmrN += 1U;
	}
}

void CModeConv::putDMRHeader()
{
	unsigned char vch[9U];

	::memset(vch, 0, 9U);

	m_NXDN.addData(&TAG_HEADER, 1U);
	m_NXDN.addData(vch, 9U);
	m_nxdnN += 1U;
}

void CModeConv::putDMREOT()
{
	unsigned char vch[9U];

	::memset(vch, 0, 9U);
	
	unsigned int fill = 4U - (m_nxdnN % 4U);
	for (unsigned int i = 0U; i < fill; i++) {
		m_NXDN.addData(&TAG_DATA, 1U);
		m_NXDN.addData(AMBE_SILENCE, 9U);
		m_nxdnN += 1U;
	}

	m_NXDN.addData(&TAG_EOT, 1U);
	m_NXDN.addData(vch, 9U);
	m_nxdnN += 1U;
}

void CModeConv::putNXDNHeader()
{
	unsigned char v_dmr[9U];

	::memset(v_dmr, 0U, 9U);

	m_DMR.addData(&TAG_HEADER, 1U);
	m_DMR.addData(v_dmr, 9U);
	m_dmrN += 1U;
}

void CModeConv::putNXDNEOT()
{
	unsigned char v_dmr[9U];

	::memset(v_dmr, 0U, 9U);
	
	unsigned int fill = 3U - (m_dmrN % 3U);
	for (unsigned int i = 0U; i < fill; i++) {
		m_DMR.addData(&TAG_DATA, 1U);
		m_DMR.addData(AMBE_SILENCE, 9U);
		m_dmrN += 1U;
	}

	m_DMR.addData(&TAG_EOT, 1U);
	m_DMR.addData(v_dmr, 9U);
	m_dmrN += 1U;
}

unsigned int CModeConv::getDMR(unsigned char* data)
{
	unsigned char tmp[9U];
	unsigned char tag[1U];

	tag[0U] = TAG_NODATA;

	if (m_dmrN >= 1U) {
		m_DMR.peek(tag, 1U);

		if (tag[0U] != TAG_DATA) {
			m_DMR.getData(tag, 1U);
			m_DMR.getData(data, 9U);
			m_dmrN -= 1U;
			return tag[0U];
		}
	}

	if (m_dmrN >= 3U) {
		m_DMR.getData(tag, 1U);
		m_DMR.getData(data, 9U);
		m_dmrN -= 1U;

		m_DMR.getData(tag, 1U);
		m_DMR.getData(tmp, 9U);
		m_dmrN -= 1U;

		::memcpy(data + 9U, tmp, 4U);
		data[13U] = tmp[4U] & 0xF0U;
		data[19U] = tmp[4U] & 0x0FU;
		::memcpy(data + 20U, tmp + 5U, 4U);

		m_DMR.getData(tag, 1U);
		m_DMR.getData(data + 24U, 9U);
		m_dmrN -= 1U;

		return TAG_DATA;
	}
	else
		return TAG_NODATA;
}

unsigned int CModeConv::getNXDN(unsigned char* data)
{
	unsigned char tag[1U];

	tag[0U] = TAG_NODATA;

	data += NXDN_FSW_LICH_SACCH_LENGTH_BYTES;
	
	if (m_nxdnN >= 1U) {
		m_NXDN.peek(tag, 1U);

		if (tag[0U] != TAG_DATA) {
			m_NXDN.getData(tag, 1U);
			m_NXDN.getData(data, 9U);
			m_nxdnN -= 1U;
			return tag[0U];
		}
	}

	if (m_nxdnN >= 4U) {
		m_NXDN.getData(tag, 1U);
		m_NXDN.getData(data, 9U);
		m_nxdnN -= 1U;

		data += 9U;
		m_NXDN.getData(tag, 1U);
		m_NXDN.getData(data, 9U);
		m_nxdnN -= 1U;

		data += 9U;
		m_NXDN.getData(tag, 1U);
		m_NXDN.getData(data, 9U);
		m_nxdnN -= 1U;

		data += 9U;
		m_NXDN.getData(tag, 1U);
		m_NXDN.getData(data, 9U);
		m_nxdnN -= 1U;

		return TAG_DATA;
	}
	else
		return TAG_NODATA;
}
