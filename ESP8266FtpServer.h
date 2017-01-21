
/*
 * FTP SERVER FOR ESP8266
 * based on FTP Serveur for Arduino Due and Ethernet shield (W5100) or WIZ820io (W5200)
 * based on Jean-Michel Gallego's work
 * modified to work with esp8266 SPIFFS by David Paiva (david@nailbuster.com)
 * modified to work with VFATFS by Zhenyu Wu (Adam_5Wu@hotmail.com)
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*******************************************************************************
 **                                                                            **
 **                      DEFINITIONS FOR FTP SERVER                            **
 **                                                                            **
 *******************************************************************************/

// Uncomment to print debugging info to console attached to ESP8266
//#define FTP_DEBUG

#ifndef FTP_SERVERESP_H
#define FTP_SERVERESP_H

#include <FS.h>
#include <ESP8266WiFi.h>
  
#define FTP_SERVER_VERSION "0.1"

#define FTP_CTRL_PORT       21         // Command port on wich server is listening  
#define FTP_DATA_PORT_PASV  50009      // Data port in passive mode

#define FTP_AUTH_TIME_OUT 30           // Max 30 seconds before log in
#define FTP_IDLE_TIME_OUT 2 * 60       // Disconnect idle client after 2 minutes of inactivity
#define FTP_DATA_TIME_OUT 10           // Wait for 10 seconds for data connection
#define FTP_FIL_SIZE 255               // Max size of a file name
#define FTP_CMD_SIZE FTP_FIL_SIZE + 8  // Max size of a command
#define FTP_BUF_SIZE 4096              // Size of file buffer for read/write
  
class FtpServer {
public:
  class Auth {
  public:
    virtual bool setUser(char const* name) = 0;
    virtual bool checkPass(char const* pass) = 0;
  };
  
protected:
  static class AnonyAuth: public Auth {
  public:
    bool setUser(char const* name) override {
      return strcmp(name, "anonymous") == 0;
    }
    bool checkPass(char const* pass) override {
      return true;
    }
  } Anonymous;
  
public:
  FtpServer(FS& fs, Auth& auth = Anonymous)
  : _fs(fs), _auth(auth) {}
  
  void    begin();
  void    handleFTP();
  
private:
  void    iniVariables();
  void    clientConnected();
  void    disconnectClient();
  boolean userIdentity();
  boolean userPassword();
  boolean processCommand();
  boolean dataConnect();
  boolean doRetrieve();
  boolean doStore();
  void    closeTransfer();
  void    abortTransfer();
  
  int8_t  readCmd();

  FS& _fs;
  Auth& _auth;
  
  WiFiClient client;
  WiFiClient data;

  File file;
  Dir dir;
  
  char     buf[ FTP_BUF_SIZE ];       // data buffer for transfers
  char     cmdLine[ FTP_CMD_SIZE ];   // where to store incoming char from client
  char     command[ 5 ];              // command sent by client
  String   renameFrom;                // previous rename-from command
  char *   parameters;                // point to begin of parameters sent by client
  uint16_t iCL;                       // pointer to cmdLine next incoming char
  int8_t   cmdStatus,                 // status of ftp command connexion
           transferStatus;            // status of ftp data transfer
  uint32_t tsEndConnection;           // projected timeout timestamp
  #ifdef FTP_DEBUG
  time_t   tsBeginTrans;              // store time of beginning of a transaction
  size_t   bytesTransfered;           // store total bytes transferred
  #endif
};

#endif // FTP_SERVERESP_H
