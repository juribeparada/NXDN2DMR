/*
*   Copyright (C) 2016,2017 by Jonathan Naylor G4KLX
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

#include "NXDN2DMR.h"

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pwd.h>
#endif

#define DMR_FRAME_PER       55U
#define NXDN_FRAME_PER      75U

const unsigned char SCRAMBLER[] = {
	0x00U, 0x00U, 0x00U, 0x82U, 0xA0U, 0x88U, 0x8AU, 0x00U, 0xA2U, 0xA8U, 0x82U, 0x8AU, 0x82U, 0x02U,
	0x20U, 0x08U, 0x8AU, 0x20U, 0xAAU, 0xA2U, 0x82U, 0x08U, 0x22U, 0x8AU, 0xAAU, 0x08U, 0x28U, 0x88U,
	0x28U, 0x28U, 0x00U, 0x0AU, 0x02U, 0x82U, 0x20U, 0x28U, 0x82U, 0x2AU, 0xAAU, 0x20U, 0x22U, 0x80U,
	0xA8U, 0x8AU, 0x08U, 0xA0U, 0xAAU, 0x02U };

#if defined(_WIN32) || defined(_WIN64)
const char* DEFAULT_INI_FILE = "NXDN2DMR.ini";
#else
const char* DEFAULT_INI_FILE = "/etc/NXDN2DMR.ini";
#endif

#include <functional>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <clocale>
#include <cctype>

int end = 0;

#if !defined(_WIN32) && !defined(_WIN64)
void sig_handler(int signo)
{
	if (signo == SIGTERM) {
		end = 1;
		::fprintf(stdout, "Received SIGTERM\n");
	}
}
#endif

int main(int argc, char** argv)
{
	const char* iniFile = DEFAULT_INI_FILE;
	if (argc > 1) {
		for (int currentArg = 1; currentArg < argc; ++currentArg) {
			std::string arg = argv[currentArg];
			if ((arg == "-v") || (arg == "--version")) {
				::fprintf(stdout, "NXDN2DMR version %s\n", VERSION);
				return 0;
			} else if (arg.substr(0, 1) == "-") {
				::fprintf(stderr, "Usage: NXDN2DMR [-v|--version] [filename]\n");
				return 1;
			} else {
				iniFile = argv[currentArg];
			}
		}
	}

#if !defined(_WIN32) && !defined(_WIN64)
	// Capture SIGTERM to finish gracelessly
	if (signal(SIGTERM, sig_handler) == SIG_ERR) 
		::fprintf(stdout, "Can't catch SIGTERM\n");
#endif

	CNXDN2DMR* gateway = new CNXDN2DMR(std::string(iniFile));

	int ret = gateway->run();

	delete gateway;

	return ret;
}

CNXDN2DMR::CNXDN2DMR(const std::string& configFile) :
m_callsign(),
m_conf(configFile),
m_dmrNetwork(NULL),
m_netSrc(0U),
m_netDst(0U),
m_nxdnSrc(0U),
m_nxdnDst(0U),
m_dmrLastDT(0U),
m_dmrFrames(0U),
m_nxdnFrames(0U),
m_dmrinfo(false)
{
	::memset(m_nxdnFrame, 0U, 200U);
	::memset(m_dmrFrame, 0U, 50U);
}

CNXDN2DMR::~CNXDN2DMR()
{
}

int CNXDN2DMR::run()
{
	bool ret = m_conf.read();
	if (!ret) {
		::fprintf(stderr, "NXDN2DMR: cannot read the .ini file\n");
		return 1;
	}

	setlocale(LC_ALL, "C");

	unsigned int logDisplayLevel = m_conf.getLogDisplayLevel();

#if !defined(_WIN32) && !defined(_WIN64)
	if(m_conf.getDaemon())
		logDisplayLevel = 0U;
#endif

	ret = ::LogInitialise(m_conf.getLogFilePath(), m_conf.getLogFileRoot(), m_conf.getLogFileLevel(), logDisplayLevel);
	if (!ret) {
		::fprintf(stderr, "NXDN2DMR: unable to open the log file\n");
		return 1;
	}

#if !defined(_WIN32) && !defined(_WIN64)
	bool m_daemon = m_conf.getDaemon();
	if (m_daemon) {
		// Create new process
		pid_t pid = ::fork();
		if (pid == -1) {
			::LogWarning("Couldn't fork() , exiting");
			return -1;
		} else if (pid != 0)
			exit(EXIT_SUCCESS);

		// Create new session and process group
		if (::setsid() == -1) {
			::LogWarning("Couldn't setsid(), exiting");
			return -1;
		}

		// Set the working directory to the root directory
		if (::chdir("/") == -1) {
			::LogWarning("Couldn't cd /, exiting");
			return -1;
		}

		::close(STDIN_FILENO);
		::close(STDOUT_FILENO);
		::close(STDERR_FILENO);

		//If we are currently root...
		if (getuid() == 0) {
			struct passwd* user = ::getpwnam("mmdvm");
			if (user == NULL) {
				::LogError("Could not get the mmdvm user, exiting");
				return -1;
			}

			uid_t mmdvm_uid = user->pw_uid;
			gid_t mmdvm_gid = user->pw_gid;

			//Set user and group ID's to mmdvm:mmdvm
			if (setgid(mmdvm_gid) != 0) {
				::LogWarning("Could not set mmdvm GID, exiting");
				return -1;
			}

			if (setuid(mmdvm_uid) != 0) {
				::LogWarning("Could not set mmdvm UID, exiting");
				return -1;
			}

			//Double check it worked (AKA Paranoia) 
			if (setuid(0) != -1) {
				::LogWarning("It's possible to regain root - something is wrong!, exiting");
				return -1;
			}
		}
	}
#endif

	m_callsign = m_conf.getCallsign();

	bool debug               = m_conf.getDMRNetworkDebug();
	in_addr dstAddress       = CUDPSocket::lookup(m_conf.getDstAddress());
	unsigned int dstPort     = m_conf.getDstPort();
	std::string localAddress = m_conf.getLocalAddress();
	unsigned int localPort   = m_conf.getLocalPort();

	m_nxdnNetwork = new CNXDNNetwork(localAddress, localPort, m_callsign, debug);
	m_nxdnNetwork->setDestination(dstAddress, dstPort);

	ret = m_nxdnNetwork->open();
	if (!ret) {
		::LogError("Cannot open the NXDN network port");
		::LogFinalise();
		return 1;
	}

	ret = createDMRNetwork();
	if (!ret) {
		::LogError("Cannot open DMR Network");
		::LogFinalise();
		return 1;
	}
	
	std::string lookupFile  = m_conf.getDMRIdLookupFile();
	unsigned int reloadTime = m_conf.getDMRIdLookupTime();

	m_lookup = new CDMRLookup(lookupFile, reloadTime);
	m_lookup->read();

	if (m_dmrpc)
		m_dmrflco = FLCO_USER_USER;
	else
		m_dmrflco = FLCO_GROUP;

	CTimer networkWatchdog(100U, 0U, 1500U);
	CTimer pollTimer(1000U, 5U);

	std::string name = m_conf.getDescription();

	CStopWatch stopWatch;
	CStopWatch nxdnWatch;
	CStopWatch dmrWatch;
	stopWatch.start();
	nxdnWatch.start();
	dmrWatch.start();
	pollTimer.start();

	unsigned char nxdn_cnt = 0;
	unsigned char dmr_cnt = 0;

	LogMessage("Starting NXDN2DMR-%s", VERSION);

	for (; end == 0;) {
		unsigned char buffer[2000U];

		CDMRData tx_dmrdata;
		unsigned int ms = stopWatch.elapsed();

		while (m_nxdnNetwork->read(buffer) > 0U) {
			if (::memcmp(buffer, "NXDND", 5U) == 0U) {
				CNXDNLICH lich;
				bool end = (buffer[7U] & 0x04) == 0x04;
				bool grp = (buffer[7U] & 0x01) == 0x01;

				scrambler(buffer + 11U);

				if (lich.decode(buffer + 11U)) {
					unsigned char usc = lich.getFCT();
					unsigned char opt = lich.getOption();

					if (usc == NXDN_LICH_USC_SACCH_NS) {
						if (end) {
							LogMessage("NXDN received end of voice transmission, %.1f seconds", float(m_nxdnFrames) / 12.5F);
							m_conv.putNXDNEOT();
							m_nxdnFrames = 0U;
						} else {
							m_nxdnSrc = (buffer[5U] << 8) | buffer[6U];
							m_nxdnDst = (buffer[8U] << 8) | buffer[9U];
							LogMessage("Received NXDN Header from %d to %s%d", m_nxdnSrc, grp ? "TG " : "", m_nxdnDst);
							m_conv.putNXDNHeader();
							m_nxdnFrames = 0U;
						}
					} else {
						if (opt == NXDN_LICH_STEAL_NONE) {
							m_conv.putNXDN(buffer + 11U);
							m_nxdnFrames++;
						}
					}
				}
			}
		}

		if (dmrWatch.elapsed() > DMR_FRAME_PER) {
			unsigned int dmrFrameType = m_conv.getDMR(m_dmrFrame);

			if(dmrFrameType == TAG_HEADER) {
				CDMRData rx_dmrdata;
				dmr_cnt = 0U;

				rx_dmrdata.setSlotNo(2U);
				rx_dmrdata.setSrcId(m_srcid);
				rx_dmrdata.setDstId(m_dstid);
				rx_dmrdata.setFLCO(m_dmrflco);
				rx_dmrdata.setN(0U);
				rx_dmrdata.setSeqNo(0U);
				rx_dmrdata.setBER(0U);
				rx_dmrdata.setRSSI(0U);
				rx_dmrdata.setDataType(DT_VOICE_LC_HEADER);

				// Add sync
				CSync::addDMRDataSync(m_dmrFrame, 0);

				// Add SlotType
				CDMRSlotType slotType;
				slotType.setColorCode(m_colorcode);
				slotType.setDataType(DT_VOICE_LC_HEADER);
				slotType.getData(m_dmrFrame);
	
				// Full LC
				CDMRLC dmrLC = CDMRLC(m_dmrflco, m_srcid, m_dstid);
				CDMRFullLC fullLC;
				fullLC.encode(dmrLC, m_dmrFrame, DT_VOICE_LC_HEADER);
				m_EmbeddedLC.setLC(dmrLC);
				
				rx_dmrdata.setData(m_dmrFrame);
				//CUtils::dump(1U, "DMR data:", m_dmrFrame, 33U);

				for (unsigned int i = 0U; i < 3U; i++) {
					rx_dmrdata.setSeqNo(dmr_cnt);
					m_dmrNetwork->write(rx_dmrdata);
					dmr_cnt++;
				}

				dmrWatch.start();
			}
			else if(dmrFrameType == TAG_EOT) {
				CDMRData rx_dmrdata;
				unsigned int n_dmr = (dmr_cnt - 3U) % 6U;
				unsigned int fill = (6U - n_dmr);
				
				if (n_dmr) {
					for (unsigned int i = 0U; i < fill; i++) {

						CDMREMB emb;
						CDMRData rx_dmrdata;

						rx_dmrdata.setSlotNo(2U);
						rx_dmrdata.setSrcId(m_srcid);
						rx_dmrdata.setDstId(m_dstid);
						rx_dmrdata.setFLCO(m_dmrflco);
						rx_dmrdata.setN(n_dmr);
						rx_dmrdata.setSeqNo(dmr_cnt);
						rx_dmrdata.setBER(0U);
						rx_dmrdata.setRSSI(0U);
						rx_dmrdata.setDataType(DT_VOICE);

						::memcpy(m_dmrFrame, DMR_SILENCE_DATA, DMR_FRAME_LENGTH_BYTES);

						// Generate the Embedded LC
						unsigned char lcss = m_EmbeddedLC.getData(m_dmrFrame, n_dmr);

						// Generate the EMB
						emb.setColorCode(m_colorcode);
						emb.setLCSS(lcss);
						emb.getData(m_dmrFrame);

						rx_dmrdata.setData(m_dmrFrame);
				
						//CUtils::dump(1U, "DMR data:", m_dmrFrame, 33U);
						m_dmrNetwork->write(rx_dmrdata);

						n_dmr++;
						dmr_cnt++;
					}
				}

				rx_dmrdata.setSlotNo(2U);
				rx_dmrdata.setSrcId(m_srcid);
				rx_dmrdata.setDstId(m_dstid);
				rx_dmrdata.setFLCO(m_dmrflco);
				rx_dmrdata.setN(n_dmr);
				rx_dmrdata.setSeqNo(dmr_cnt);
				rx_dmrdata.setBER(0U);
				rx_dmrdata.setRSSI(0U);
				rx_dmrdata.setDataType(DT_TERMINATOR_WITH_LC);

				// Add sync
				CSync::addDMRDataSync(m_dmrFrame, 0);

				// Add SlotType
				CDMRSlotType slotType;
				slotType.setColorCode(m_colorcode);
				slotType.setDataType(DT_TERMINATOR_WITH_LC);
				slotType.getData(m_dmrFrame);
	
				// Full LC
				CDMRLC dmrLC = CDMRLC(m_dmrflco, m_srcid, m_dstid);
				CDMRFullLC fullLC;
				fullLC.encode(dmrLC, m_dmrFrame, DT_TERMINATOR_WITH_LC);
				
				rx_dmrdata.setData(m_dmrFrame);
				//CUtils::dump(1U, "DMR data:", m_dmrFrame, 33U);
				m_dmrNetwork->write(rx_dmrdata);

				dmrWatch.start();
			}
			else if(dmrFrameType == TAG_DATA) {
				CDMREMB emb;
				CDMRData rx_dmrdata;
				unsigned int n_dmr = (dmr_cnt - 3U) % 6U;

				rx_dmrdata.setSlotNo(2U);
				rx_dmrdata.setSrcId(m_srcid);
				rx_dmrdata.setDstId(m_dstid);
				rx_dmrdata.setFLCO(m_dmrflco);
				rx_dmrdata.setN(n_dmr);
				rx_dmrdata.setSeqNo(dmr_cnt);
				rx_dmrdata.setBER(0U);
				rx_dmrdata.setRSSI(0U);
			
				if (!n_dmr) {
					rx_dmrdata.setDataType(DT_VOICE_SYNC);
					// Add sync
					CSync::addDMRAudioSync(m_dmrFrame, 0U);
					// Prepare Full LC data
					CDMRLC dmrLC = CDMRLC(m_dmrflco, m_srcid, m_dstid);
					// Configure the Embedded LC
					m_EmbeddedLC.setLC(dmrLC);
				}
				else {
					rx_dmrdata.setDataType(DT_VOICE);
					// Generate the Embedded LC
					unsigned char lcss = m_EmbeddedLC.getData(m_dmrFrame, n_dmr);
					// Generate the EMB
					emb.setColorCode(m_colorcode);
					emb.setLCSS(lcss);
					emb.getData(m_dmrFrame);
				}

				rx_dmrdata.setData(m_dmrFrame);
				
				//CUtils::dump(1U, "DMR data:", m_dmrFrame, 33U);
				m_dmrNetwork->write(rx_dmrdata);

				dmr_cnt++;
				dmrWatch.start();
			}
		}

		while (m_dmrNetwork->read(tx_dmrdata) > 0U) {
			m_netSrc = tx_dmrdata.getSrcId();
			m_netDst = tx_dmrdata.getDstId();
			
			FLCO netflco = tx_dmrdata.getFLCO();
			unsigned char DataType = tx_dmrdata.getDataType();

			if (!tx_dmrdata.isMissing()) {
				networkWatchdog.start();

				if(DataType == DT_TERMINATOR_WITH_LC) {
					LogMessage("DMR received end of voice transmission, %.1f seconds", float(m_dmrFrames) / 16.667F);

					m_conv.putDMREOT();
					m_dmrNetwork->reset(2U);
					networkWatchdog.stop();
					m_dmrFrames = 0U;
					m_dmrinfo = false;
				}

				if((DataType == DT_VOICE_LC_HEADER) && (DataType != m_dmrLastDT)) {
					std::string netSrc = m_lookup->findCS(m_netSrc);
					std::string netDst = (netflco == FLCO_GROUP ? "TG " : "") + m_lookup->findCS(m_netDst);

					m_conv.putDMRHeader();
					LogMessage("DMR audio received from %s to %s", netSrc.c_str(), netDst.c_str());

					m_dmrinfo = true;

					m_dmrFrames = 0U;
				}

				if(DataType == DT_VOICE_SYNC || DataType == DT_VOICE) {
					unsigned char dmr_frame[50];
					tx_dmrdata.getData(dmr_frame);
					m_conv.putDMR(dmr_frame); // Add DMR frame for NXDN conversion
					m_dmrFrames++;
				}
			}
			else {
				if(DataType == DT_VOICE_SYNC || DataType == DT_VOICE) {
					unsigned char dmr_frame[50];
					tx_dmrdata.getData(dmr_frame);

					if (!m_dmrinfo) {
						std::string netSrc = m_lookup->findCS(m_netSrc);
						std::string netDst = (netflco == FLCO_GROUP ? "TG " : "") + m_lookup->findCS(m_netDst);

						LogMessage("DMR audio received from %s to %s", netSrc.c_str(), netDst.c_str());

						m_dmrinfo = true;
					}

					m_conv.putDMR(dmr_frame); // Add DMR frame for NXDN conversion
					m_dmrFrames++;
				}

				networkWatchdog.clock(ms);
				if (networkWatchdog.hasExpired()) {
					LogDebug("Network watchdog has expired, %.1f seconds", float(m_dmrFrames) / 16.667F);
					m_dmrNetwork->reset(2U);
					networkWatchdog.stop();
					m_dmrFrames = 0U;
					m_dmrinfo = false;
				}
			}
			
			m_dmrLastDT = DataType;
		}

		if (nxdnWatch.elapsed() > NXDN_FRAME_PER) {
			unsigned int nxdnFrameType = m_conv.getNXDN(m_nxdnFrame + 11U);
			unsigned int netSrc = truncID(m_netSrc);
			unsigned int netDst = truncID(m_netDst);

			if(nxdnFrameType == TAG_HEADER) {
				nxdn_cnt = 0U;

				::memcpy(m_nxdnFrame + 0U, "NXDND", 5U);
				m_nxdnFrame[5U] = (netSrc >> 8) & 0xFF;
				m_nxdnFrame[6U] = (netSrc >> 0) & 0xFF;
				m_nxdnFrame[7U] = (!m_dmrpc) & 0x01;
				m_nxdnFrame[8U] = (netDst >> 8) & 0xFF;
				m_nxdnFrame[9U] = (netDst >> 0) & 0xFF;
				m_nxdnFrame[10U] = nxdn_cnt;

				// Add the NXDN Sync
				CSync::addNXDNSync(m_nxdnFrame + 11U);

				CNXDNLICH lich;
				lich.setRFCT(NXDN_LICH_RFCT_RDCH);
				lich.setFCT(NXDN_LICH_USC_SACCH_NS);
				lich.setOption(NXDN_LICH_STEAL_FACCH);
				lich.setDirection(NXDN_LICH_DIRECTION_INBOUND);
				lich.encode(m_nxdnFrame + 11U);

				CNXDNSACCH sacch;
				sacch.setRAN(0x01);
				sacch.setStructure(NXDN_SR_SINGLE);
				sacch.setData(SACCH_IDLE);
				sacch.encode(m_nxdnFrame + 11U);

				unsigned char layer3data[25U];
				CNXDNLayer3 layer3;
				layer3.setMessageType(NXDN_MESSAGE_TYPE_VCALL);
				layer3.setSourceUnitId(netSrc & 0xFFFF);
				layer3.setDestinationGroupId(netDst & 0xFFFF);
				layer3.setGroup(true);
				layer3.setDataBlocks(0U);
				layer3.getData(layer3data);

				CNXDNFACCH1 facch;
				facch.setData(layer3data);
				facch.encode(m_nxdnFrame + 11U, NXDN_FSW_LENGTH_BITS + NXDN_LICH_LENGTH_BITS + NXDN_SACCH_LENGTH_BITS);
				facch.encode(m_nxdnFrame + 11U, NXDN_FSW_LENGTH_BITS + NXDN_LICH_LENGTH_BITS + NXDN_SACCH_LENGTH_BITS + NXDN_FACCH1_LENGTH_BITS);

				scrambler(m_nxdnFrame + 11U);
				m_nxdnNetwork->write(m_nxdnFrame);

				nxdnWatch.start();
			}
			else if (nxdnFrameType == TAG_EOT) {
				::memcpy(m_nxdnFrame + 0U, "NXDND", 5U);
				m_nxdnFrame[5U] = (netSrc >> 8) & 0xFF;
				m_nxdnFrame[6U] = (netSrc >> 0) & 0xFF;
				m_nxdnFrame[7U] = ((!m_dmrpc) & 0x01) | 0x04;
				m_nxdnFrame[8U] = (netDst >> 8) & 0xFF;
				m_nxdnFrame[9U] = (netDst >> 0) & 0xFF;
				m_nxdnFrame[10U] = nxdn_cnt;

				// Add the NXDN Sync
				CSync::addNXDNSync(m_nxdnFrame + 11U);

				CNXDNLICH lich;
				lich.setRFCT(NXDN_LICH_RFCT_RDCH);
				lich.setFCT(NXDN_LICH_USC_SACCH_NS);
				lich.setOption(NXDN_LICH_STEAL_FACCH);
				lich.setDirection(NXDN_LICH_DIRECTION_INBOUND);
				lich.encode(m_nxdnFrame + 11U);

				CNXDNSACCH sacch;
				sacch.setRAN(0x01);
				sacch.setStructure(NXDN_SR_SINGLE);
				sacch.setData(SACCH_IDLE);
				sacch.encode(m_nxdnFrame + 11U);

				unsigned char layer3data[25U];
				CNXDNLayer3 layer3;
				layer3.setMessageType(NXDN_MESSAGE_TYPE_TX_REL);
				layer3.setSourceUnitId(netSrc & 0xFFFF);
				layer3.setDestinationGroupId(netDst & 0xFFFF);
				layer3.setGroup(true);
				layer3.setDataBlocks(0U);
				layer3.getData(layer3data);

				CNXDNFACCH1 facch;
				facch.setData(layer3data);
				facch.encode(m_nxdnFrame + 11U, NXDN_FSW_LENGTH_BITS + NXDN_LICH_LENGTH_BITS + NXDN_SACCH_LENGTH_BITS);
				facch.encode(m_nxdnFrame + 11U, NXDN_FSW_LENGTH_BITS + NXDN_LICH_LENGTH_BITS + NXDN_SACCH_LENGTH_BITS + NXDN_FACCH1_LENGTH_BITS);

				scrambler(m_nxdnFrame + 11U);
				m_nxdnNetwork->write(m_nxdnFrame);

				nxdn_cnt = 0U;
			}
			else if (nxdnFrameType == TAG_DATA) {
				::memcpy(m_nxdnFrame + 0U, "NXDND", 5U);
				m_nxdnFrame[5U] = (netSrc >> 8) & 0xFF;
				m_nxdnFrame[6U] = (netSrc >> 0) & 0xFF;
				m_nxdnFrame[7U] = (!m_dmrpc) & 0x01;
				m_nxdnFrame[8U] = (netDst >> 8) & 0xFF;
				m_nxdnFrame[9U] = (netDst >> 0) & 0xFF;
				m_nxdnFrame[10U] = nxdn_cnt;

				// Add the NXDN Sync
				CSync::addNXDNSync(m_nxdnFrame + 11U);

				CNXDNLICH lich;
				lich.setRFCT(NXDN_LICH_RFCT_RDCH);
				lich.setFCT(NXDN_LICH_USC_SACCH_SS);
				lich.setOption(NXDN_LICH_STEAL_NONE);
				lich.setDirection(NXDN_LICH_DIRECTION_INBOUND);
				lich.encode(m_nxdnFrame + 11U);

				CNXDNSACCH sacch;
				CNXDNLayer3 layer3;
				unsigned char message[3U];

				layer3.setMessageType(NXDN_MESSAGE_TYPE_VCALL);
				layer3.setSourceUnitId(netSrc & 0xFFFF);
				layer3.setDestinationGroupId(netDst & 0xFFFF);
				layer3.setGroup(true);
				layer3.setDataBlocks(0U);

				switch (nxdn_cnt % 4) {
					case 0:
						sacch.setStructure(NXDN_SR_1_4);
						layer3.encode(message, 18U, 0U);
						sacch.setData(message);
						break;
					case 1:
						sacch.setStructure(NXDN_SR_2_4);
						layer3.encode(message, 18U, 18U);
						sacch.setData(message);
						break;
					case 2:
						sacch.setStructure(NXDN_SR_3_4);
						layer3.encode(message, 18U, 36U);
						sacch.setData(message);
						break;
					case 3:
						sacch.setStructure(NXDN_SR_4_4);
						layer3.encode(message, 18U, 54U);
						sacch.setData(message);
						break;
				}

				sacch.setRAN(0x01);
				sacch.encode(m_nxdnFrame + 11U);

				// Send data to MMDVMHost
				scrambler(m_nxdnFrame + 11U);
				m_nxdnNetwork->write(m_nxdnFrame);
				
				nxdn_cnt++;
				nxdnWatch.start();
			}
		}

		stopWatch.start();

		m_nxdnNetwork->clock(ms);
		m_dmrNetwork->clock(ms);

		pollTimer.clock(ms);
		if (pollTimer.isRunning() && pollTimer.hasExpired()) {
			m_nxdnNetwork->writePoll();
			pollTimer.start();
		}

		if (ms < 5U)
			CThread::sleep(5U);
	}

	m_nxdnNetwork->close();
	m_dmrNetwork->close();
	delete m_dmrNetwork;
	delete m_nxdnNetwork;

	::LogFinalise();

	return 0;
}

void CNXDN2DMR::scrambler(unsigned char* data) const
{
	assert(data != NULL);

	for (unsigned int i = 0U; i < NXDN_FRAME_LENGTH_BYTES; i++)
		data[i] ^= SCRAMBLER[i];
}

unsigned int CNXDN2DMR::truncID(unsigned int id)
{
	char temp[20];

	snprintf(temp, 8, "%0.7d", id);
	unsigned int newid = atoi(temp + 2);

	if (newid > 65519)
		newid = 65519;

	if (newid == 0)
		newid = 1;

	return newid;
}

bool CNXDN2DMR::createDMRNetwork()
{
	std::string address  = m_conf.getDMRNetworkAddress();
	unsigned int port    = m_conf.getDMRNetworkPort();
	unsigned int local   = m_conf.getDMRNetworkLocal();
	std::string password = m_conf.getDMRNetworkPassword();
	bool debug           = m_conf.getDMRNetworkDebug();
	unsigned int jitter  = m_conf.getDMRNetworkJitter();
	bool slot1           = false;
	bool slot2           = true;
	bool duplex          = false;
	HW_TYPE hwType       = HWT_MMDVM;

	m_srcHS = m_conf.getDMRId();
	m_colorcode = 1U;
	m_dstid = m_conf.getDMRDstId();
	m_dmrpc = m_conf.getDMRPC();

	if (m_srcHS > 99999999U)
		m_defsrcid = m_srcHS / 100U;
	else if (m_srcHS > 9999999U)
		m_defsrcid = m_srcHS / 10U;
	else
		m_defsrcid = m_srcHS;

	m_srcid = m_defsrcid;
	
	LogMessage("DMR Network Parameters");
	LogMessage("    ID: %u", m_srcHS);
	LogMessage("    Default SrcID: %u", m_defsrcid);
	LogMessage("    Startup DstID: %s%u", m_dmrpc ? "" : "TG ", m_dstid);
	LogMessage("    Address: %s", address.c_str());
	LogMessage("    Port: %u", port);
	if (local > 0U)
		LogMessage("    Local: %u", local);
	else
		LogMessage("    Local: random");
	LogMessage("    Jitter: %ums", jitter);

	m_dmrNetwork = new CDMRNetwork(address, port, local, m_srcHS, password, duplex, VERSION, debug, slot1, slot2, hwType, jitter);

	std::string options = m_conf.getDMRNetworkOptions();
	if (!options.empty()) {
		LogMessage("    Options: %s", options.c_str());
		m_dmrNetwork->setOptions(options);
	}

	unsigned int rxFrequency = m_conf.getRxFrequency();
	unsigned int txFrequency = m_conf.getTxFrequency();
	unsigned int power       = m_conf.getPower();
	float latitude           = m_conf.getLatitude();
	float longitude          = m_conf.getLongitude();
	int height               = m_conf.getHeight();
	std::string location     = m_conf.getLocation();
	std::string description  = m_conf.getDescription();
	std::string url          = m_conf.getURL();

	LogMessage("Info Parameters");
	LogMessage("    Callsign: %s", m_callsign.c_str());
	LogMessage("    RX Frequency: %uHz", rxFrequency);
	LogMessage("    TX Frequency: %uHz", txFrequency);
	LogMessage("    Power: %uW", power);
	LogMessage("    Latitude: %fdeg N", latitude);
	LogMessage("    Longitude: %fdeg E", longitude);
	LogMessage("    Height: %um", height);
	LogMessage("    Location: \"%s\"", location.c_str());
	LogMessage("    Description: \"%s\"", description.c_str());
	LogMessage("    URL: \"%s\"", url.c_str());

	m_dmrNetwork->setConfig(m_callsign, rxFrequency, txFrequency, power, m_colorcode, latitude, longitude, height, location, description, url);

	bool ret = m_dmrNetwork->open();
	if (!ret) {
		delete m_dmrNetwork;
		m_dmrNetwork = NULL;
		return false;
	}

	m_dmrNetwork->enable(true);

	return true;
}
