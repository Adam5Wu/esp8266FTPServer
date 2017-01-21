/*
 * FTP Serveur for ESP8266
 * based on FTP Serveur for Arduino Due and Ethernet shield (W5100) or WIZ820io (W5200)
 * based on Jean-Michel Gallego's work
 * modified to work with esp8266 SPIFFS by David Paiva david@nailbuster.com
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

#include "ESP8266FtpServer.h"

#include <time.h>

static WiFiServer ftpServer(FTP_CTRL_PORT);
static WiFiServer dataServer(FTP_DATA_PORT_PASV);

FtpServer::AnonyAuth FtpServer::Anonymous;

void FtpServer::begin()
{
  dataServer.begin();
  cmdStatus = 0;
}

void FtpServer::iniVariables()
{
  // Set the root directory
  dir = _fs.openDir("/");

  renameFrom.clear();
  transferStatus = 0;
}

void FtpServer::handleFTP()
{
  if (cmdStatus == 0)
  {
    if (client.connected())
      disconnectClient();
    cmdStatus = 1;
  }
  else if (cmdStatus == 1)         // Ftp server waiting for connection
  {
    abortTransfer();
    iniVariables();

    // Tells the ftp server to begin listening for incoming connection
    ftpServer.begin();
    #ifdef FTP_DEBUG
    Serial.println("Ftp server waiting for connection on port "+ String(FTP_CTRL_PORT));
    #endif
    cmdStatus = 2;
  }
  else if (cmdStatus == 2)         // Ftp server idle
  {
    if (ftpServer.hasClient())
      client = ftpServer.available();

    if (client.connected())        // A client connected
    {
      // We handle one client at a time
      ftpServer.stop();
      clientConnected();
      tsEndConnection = time(NULL) + FTP_AUTH_TIME_OUT;
      cmdStatus = 3;
    }
  }
  else if (readCmd() > 0)          // Got request
  {
    #ifdef FTP_DEBUG
    Serial.println("> "+String(command)+(parameters?' '+String(parameters):""));
    #endif
    if (cmdStatus == 3) {          // Ftp server waiting for user identity
      if (userIdentity())
        cmdStatus = 4;
    } else if (cmdStatus == 4) {   // Ftp server waiting for user registration
      if (userPassword()) {
        tsEndConnection = time(NULL) + FTP_IDLE_TIME_OUT;
        cmdStatus = 5;
      }
    } else if (cmdStatus == 5) {   // Ftp server waiting for user command
      if (processCommand())
        tsEndConnection = time(NULL) + FTP_IDLE_TIME_OUT;
      else
        cmdStatus = 0;
    }
  }
  else if (!client.connected() || !client)
  {
    cmdStatus = 1;
    Serial.println("* Client disconnected");
  }

  if (transferStatus == 1)         // Retrieve data
  {
    tsEndConnection = time(NULL) + FTP_IDLE_TIME_OUT;
    doRetrieve();
  }
  else if (transferStatus == 2)    // Store data
  {
    tsEndConnection = time(NULL) + FTP_IDLE_TIME_OUT;
    doStore();
  }
  else if (cmdStatus > 2 && (tsEndConnection < time(NULL)))
  {
    Serial.println("* Client timeout");
    client.println("530 Timeout");
    cmdStatus = 0;
  }
}

void FtpServer::clientConnected()
{
  #ifdef FTP_DEBUG
  Serial.println("Client connected!");
  #endif
  client.println("220 Welcome to ESP8266 FTP "+ String(FTP_SERVER_VERSION));
  iCL = 0;
}

void FtpServer::disconnectClient()
{
  #ifdef FTP_DEBUG
  Serial.println(" Disconnecting client");
  #endif
  abortTransfer();
  client.println("221 Goodbye");
  client.stop();
}

boolean FtpServer::userIdentity()
{
  if (strcmp(command, "USER"))
    client.println("500 Expect authentication");
  else if (!_auth.setUser(parameters)) {
    #ifdef FTP_DEBUG
    Serial.println("Invalid user account");
    #endif
    client.println("530 User not found");
  } else {
    Serial.println("Logging on user: "+String(parameters));
    client.println("331 OK. Password required");
    return true;
  }
  return false;
}

boolean FtpServer::userPassword()
{
  if (strcmp(command, "PASS"))
    client.println("500 Expect authentication");
  else if (!_auth.checkPass(parameters)) {
    Serial.println("Incorrect user password");
    client.println("530 Incorrect password");
  } else {
    Serial.println("User logged in, waiting for commands...");
    client.println("230 OK. Authenticated");
    return true;
  }
  return false;
}

boolean FtpServer::processCommand()
{
  ///////////////////////////////////////
  //                                   //
  //      ACCESS CONTROL COMMANDS      //
  //                                   //
  ///////////////////////////////////////

  //
  //  CDUP - Change to Parent Directory
  //
  if (!strcmp(command, "CDUP"))
  {
    client.println("250 Ok. Current directory is " + String(dir.name()));
  }
  //
  //  CWD - Change Working Directory
  //
  else if (!strcmp(command, "CWD"))
  {
    if (strcmp(parameters, ".") == 0)  // 'CWD .' is the same as PWD command
      client.println("257 \"" + String(dir.name()) + "\" is your current directory");
    else {
      Dir _dir = (*parameters != '/')? dir.openDir(parameters) : _fs.openDir(parameters);
      if (_dir.name()) {
        dir = _dir;
        client.println("250 Ok. Current directory is " + String(dir.name()));
      } else {
        client.println("550 Directory \""+String(parameters)+"\" not found");
      }
    }
  }
  //
  //  PWD - Print Directory
  //
  else if (!strcmp(command, "PWD"))
    client.println("257 \"" + String(dir.name()) + "\" is your current directory");
  //
  //  QUIT
  //
  else if (!strcmp(command, "QUIT"))
  {
    disconnectClient();
    return false;
  }

  ///////////////////////////////////////
  //                                   //
  //    TRANSFER PARAMETER COMMANDS    //
  //                                   //
  ///////////////////////////////////////

  //
  //  MODE - Transfer Mode
  //
  else if (!strcmp(command, "MODE"))
  {
    if (!strcmp(parameters, "S"))
      client.println("200 S Ok");
    else
      client.println("504 Only S(tream) mode is supported");
  }
  //
  //  PASV - Passive Connection management
  //
  else if (!strcmp(command, "PASV"))
  {
    if (data.connected()) data.stop();
    IPAddress dataIp = WiFi.localIP();
    uint16_t dataPort = FTP_DATA_PORT_PASV;
    #ifdef FTP_DEBUG
    //Serial.println("Connection management set to passive");
    //Serial.println("Data port set to " + String(dataPort));
    #endif
    client.println("227 Entering Passive Mode ("+ String(dataIp[0]) + "," + String(dataIp[1])+","+ String(dataIp[2])+","+ String(dataIp[3])+","+String(dataPort >> 8) +","+String (dataPort & 255)+").");
  }
  //
  //  STRU - File Structure
  //
  else if (!strcmp(command, "STRU"))
  {
    if (!strcmp(parameters, "F"))
      client.println("200 F Ok");
    else
      client.println("504 Only F(ile) structure is supported");
  }
  //
  //  TYPE - Data Type
  //
  else if (!strcmp(command, "TYPE"))
  {
    if (!strcmp(parameters, "A"))
      client.println("200 TYPE is now ASII");
    else if (!strcmp(parameters, "I"))
      client.println("200 TYPE is now 8-bit binary");
    else
      client.println("504 Unknown TYPE");
  }

  ///////////////////////////////////////
  //                                   //
  //        FTP SERVICE COMMANDS       //
  //                                   //
  ///////////////////////////////////////

  //
  //  ABOR - Abort
  //
  else if (!strcmp(command, "ABOR"))
  {
    abortTransfer();
    client.println("226 Data connection closed");
  }
  //
  //  DELE - Delete a File
  //
  else if (!strcmp(command, "DELE"))
  {
    if (strlen(parameters) == 0)
      client.println("501 No file name");
    else
    {
      String dirName = String(dir.name());
      String removeEntry = (*parameters != '/')? dirName+(dirName.length()>1?"/":"")+parameters : parameters;
      bool res = _fs.remove(removeEntry.c_str());
      if (res) {
        Serial.println("* Deleted " + removeEntry);
        client.println("250 Deleted " + String(parameters));
      } else
        client.println("450 Can't delete " + String(parameters));
    }
  }
  //
  //  LIST - List
  //
  else if (!strcmp(command, "LIST"))
  {
    if (!dataConnect())
      client.println("425 No data connection");
    else
    {
      client.println("150 Accepted data connection");
      uint16_t nm = 0;
      if (dir.next(true))
        do {
          String fn = dir.entryName();
          bool isDir = dir.isEntryDir();
          size_t fs = dir.entrySize();
          time_t fm = dir.entryMtime();
          // EPLF format: https://cr.yp.to/ftp/list/eplf.html
          //String listdata = "+m"+String(fm)+','+(isDir?'/':'r')+",s"+String(fs)+",\t"+fn;
          struct tm tpart;
          gmtime_r(&fm, &tpart);
          char tbuf[16];
          strftime(tbuf, 16, "%b %d %Y", &tpart);
          String listdata = String(isDir?"drwxr-xr-x":"-rw-r--r--") + " 1 root root "
                            + String(fs) + ' '+ tbuf + ' ' + fn;
          #ifdef FTP_DEBUG
          Serial.println(listdata);
          #endif
          data.println(listdata);
          nm++;
        } while(dir.next());
      client.println("226 " + String(nm) + " matches total");
      data.stop();
    }
  }
  //
  //  MLSD - Listing for Machine Processing (see RFC 3659)
  //
  else if (!strcmp(command, "MLSD"))
  {
    if (!dataConnect())
      client.println("425 No data connection MLSD");
    else
    {
      client.println("150 Accepted data connection");
      uint16_t nm = 0;
      if (dir.next(true))
        do {
          String fn = dir.entryName();
          bool isDir = dir.isEntryDir();
          size_t fs = dir.entrySize();
          time_t fm = dir.entryMtime();
          // https://tools.ietf.org/html/rfc3659
          struct tm tpart;
          gmtime_r(&fm, &tpart);
          char tbuf[16];
          sprintf(tbuf, "%04d%02d%02d%02d%02d%02d",
                  tpart.tm_year + 1900, tpart.tm_mon + 1, tpart.tm_mday,
                  tpart.tm_hour, tpart.tm_min, tpart.tm_sec);
          String listdata = "Size="+String(fs)+";Modify="+String(tbuf)+";Type="+(isDir?"dir":"file")+"; "+fn;
          #ifdef FTP_DEBUG
          Serial.println(listdata);
          #endif
          data.println(listdata);
          nm++;
        } while(dir.next());
      client.println("226 " + String(nm) + " matches total");
      data.stop();
    }
  }
  //
  //  NLST - Name List
  //
  else if (!strcmp(command, "NLST"))
  {
    if (!dataConnect())
      client.println("425 No data connection");
    else
    {
      client.println("150 Accepted data connection");
      uint16_t nm = 0;
      if (dir.next(true))
        do {
          data.println(dir.entryName());
          nm++;
        } while(dir.next());
      client.println("226 " + String(nm) + " matches total");
      data.stop();
    }
  }
  //
  //  NOOP
  //
  else if (!strcmp(command, "NOOP"))
  {
    client.println("200 Zzz...");
  }
  //
  //  SYST
  //
  else if (!strcmp(command, "SYST"))
  {
    client.println("215 UNIX Type: L8");
  }
  //
  //  RETR - Retrieve
  //
  else if (!strcmp(command, "RETR"))
  {
    if (strlen(parameters) == 0)
      client.println("501 No file name");
    else {
      File _file = (*parameters != '/')? dir.openFile(parameters,"r") : _fs.open(parameters,"r");
      if (_file.name()) {
        if (dataConnect()) {
          file = _file;
          Serial.println("* Sending " + String(file.name()));
          #ifdef FTP_DEBUG
          tsBeginTrans = time(NULL);
          bytesTransfered = 0;
          #endif
          client.println("150-Data connection established");
          client.println("150 " + String(file.size()) + " bytes to download");
          transferStatus = 1;
        } else {
          client.println("425 No data connection");
        }
      } else {
        client.println("550 File " +String(parameters)+ " not found");
      }
    }
  }
  //
  //  STOR - Store
  //
  else if (!strcmp(command, "STOR"))
  {
    if (strlen(parameters) == 0)
      client.println("501 No file name");
    else {
      File _file = (*parameters != '/')? dir.openFile(parameters,"w") : _fs.open(parameters,"w");
      if (_file.name()) {
        if (dataConnect()) {
          file = _file;
          Serial.println("* Receiving " +String(file.name()));
          #ifdef FTP_DEBUG
          tsBeginTrans = time(NULL);
          bytesTransfered = 0;
          #endif
          client.println("150 Data connection established");
          transferStatus = 2;
        } else {
          client.println("425 No data connection");
        }
      } else {
        client.println("451 Can't open/create " +String(parameters));
      }
    }
  }
  //
  //  MKD - Make Directory
  //
  else if (!strcmp(command, "MKD"))
  {
    Dir _dir = (*parameters != '/')? dir.openDir(parameters, true) : _fs.openDir(parameters, true);
    if (_dir.name()) {
      client.println("257 Create directory " + String(parameters));
    } else {
      client.println("550 Failed to create directory");
    }
  }
  //
  //  RMD - Remove a Directory
  //
  else if (!strcmp(command, "RMD"))
  {
    bool res = (*parameters != '/')? dir.remove(parameters) : _fs.remove(parameters);
    if (res) {
      client.println("250 Removed Directory " + String(parameters));
    } else {
      client.println("550 Failed to remove directory");
    }
  }
  //
  //  RNFR - Rename From
  //
  else if (!strcmp(command, "RNFR"))
  {
    if (strlen(parameters) == 0)
      client.println("501 No file name");
    else {
       String dirName = String(dir.name());
      renameFrom = (*parameters != '/')? dirName+(dirName.length()>1?"/":"")+parameters : parameters;
      bool res = _fs.exists(renameFrom);
      if (res) {
        #ifdef FTP_DEBUG
        Serial.println("Renaming from " + renameFrom);
        #endif
        client.println("350 RNFR accepted - file exists, ready for destination");
      } else {
        renameFrom.clear();
        client.println("550 File " +String(parameters)+ " not found");
      }
    }
  }
  //
  //  RNTO - Rename To
  //
  else if (!strcmp(command, "RNTO"))
  {
    if (strlen(parameters) == 0)
      client.println("501 No file name");
    else if (renameFrom.empty())
      client.println("503 Need RNFR before RNTO");
    else {
      String dirName = String(dir.name());
      String renameTo = (*parameters != '/')? dirName+(dirName.length()>1?"/":"")+parameters : parameters;
      #ifdef FTP_DEBUG
      Serial.println("Renaming to " + renameTo);
      #endif
      bool res = _fs.exists(renameTo);
      if (res) {
        client.println("553 Target file/directory exists");
      } else {
        res = _fs.rename(renameFrom, renameTo);
        if (res) {
          client.println("250 File successfully renamed or moved");
        } else {
          client.println("550 Rename/move failure");
        }
      }
    }
    renameFrom.clear();
  }

  ///////////////////////////////////////
  //                                   //
  //   EXTENSIONS COMMANDS (RFC 3659)  //
  //                                   //
  ///////////////////////////////////////

  //
  //  FEAT - New Features
  //
  else if (!strcmp(command, "FEAT"))
  {
    client.println("211-Extensions supported:");
    client.println(" MLSD");
    client.println(" MDTM");
    client.println(" SIZE");
    client.println("211 End.");
  }
  //
  //  MDTM - File Modification Time (see RFC 3659)
  //
  else if (!strcmp(command, "MDTM"))
  {
    if (strlen(parameters) == 0)
    client.println("501 No file name");
    else {
      File _file = (*parameters != '/')? dir.openFile(parameters,"r") : _fs.open(parameters,"r");
      if (_file.name()) {
        time_t fm = _file.mtime();
        struct tm tpart;
        gmtime_r(&fm, &tpart);
        char tbuf[16];
        sprintf(tbuf, "%04d%02d%02d%02d%02d%02d",
                tpart.tm_year + 1900, tpart.tm_mon + 1, tpart.tm_mday,
                tpart.tm_hour, tpart.tm_min, tpart.tm_sec);
        client.println("213 " +String(tbuf));
      } else {
        client.println("550 File " +String(parameters)+ " not found");
      }
    }
  }

  //
  //  SIZE - Size of the file
  //
  else if (!strcmp(command, "SIZE"))
  {
    if (strlen(parameters) == 0)
    client.println("501 No file name");
    else {
      File _file = (*parameters != '/')? dir.openFile(parameters,"r") : _fs.open(parameters,"r");
      if (_file.name()) {
        size_t fs = _file.size();
        client.println("213 " +String(fs));
      } else {
        client.println("550 File " +String(parameters)+ " not found");
      }
    }
  }
  //
  //  Unrecognized commands ...
  //
  else
    client.println("500 Unknown command");

  return true;
}

boolean FtpServer::dataConnect()
{
  data.stop();
  // Wait for a data connection
  unsigned long expireTime = time(NULL) + FTP_DATA_TIME_OUT;
  while (time(NULL) < expireTime) {
    if (dataServer.hasClient()) {
      data = dataServer.available();
      break;
    }
    delay(100);
  }
  return data.connected();
}

boolean FtpServer::doRetrieve()
{
  if (data.connected())
  {
    int16_t nb = file.readBytes(buf, FTP_BUF_SIZE);
    if (nb > 0)
    {
      data.write((uint8_t*) buf, nb);
      #ifdef FTP_DEBUG
      Serial.println("Sent "+String(nb)+" bytes") ;
      bytesTransfered += nb;
      #endif
      return true;
    }
  }
  closeTransfer();
  return false;
}

boolean FtpServer::doStore()
{
  if (data.connected())
  {
    int16_t nb = data.readBytes(buf, FTP_BUF_SIZE);
    if (nb > 0)
    {
      file.write((uint8_t*) buf, nb);
      #ifdef FTP_DEBUG
      Serial.println("Received "+String(nb)+" bytes") ;
      bytesTransfered += nb;
      #endif
    }
    return true;
  }
  closeTransfer();
  return false;
}

void FtpServer::closeTransfer()
{
  file.close();
  data.stop();

  client.println("226 File successfully transferred");
  transferStatus = 0;

  #ifdef FTP_DEBUG
  uint32_t deltaT = time(NULL) - tsBeginTrans;
  Serial.println("Data transfer closed (" + String(bytesTransfered) + " bytes, " + String(deltaT) + " seconds)") ;
  #endif
}

void FtpServer::abortTransfer()
{
  if (transferStatus > 0)
  {
    file.close();
    data.stop();
    client.println("426 Transfer aborted" );

    transferStatus = 0;
    #ifdef FTP_DEBUG
    Serial.println("Transfer aborted!") ;
    #endif
  }
}

// Read a char from client connected to ftp server
//
//  update cmdLine and command buffers, iCL and parameters pointers
//
//  return:
//    -2 if buffer cmdLine is full
//    -1 if line not completed
//     0 if empty line received
//    length of cmdLine (positive) if no empty line received

int8_t FtpServer::readCmd()
{
  int8_t rc = -1;

  if (client.available())
  {
    char c = client.read();
    #ifdef FTP_DEBUG
    //Serial.print(c);
    #endif
    if (c == '\\')
      c = '/';
    if (c != '\r')
      if (c != '\n')
      {
        if (iCL < FTP_CMD_SIZE)
          cmdLine[ iCL ++ ] = c;
        else
          rc = -2; //  Line too long
      }
      else
      {
        cmdLine[ iCL ] = 0;
        command[ 0 ] = 0;
        parameters = NULL;
        // empty line?
        if (iCL == 0)
          rc = 0;
        else
        {
          rc = iCL;
          // search for space between command and parameters
          parameters = strchr(cmdLine, ' ');
          if (parameters != NULL)
          {
            if (parameters - cmdLine > 4)
              rc = -2; // Syntax error
            else
            {
              strncpy(command, cmdLine, parameters - cmdLine);
              command[ parameters - cmdLine ] = 0;
              while(* (++ parameters) == ' ');
            }
          }
          else if (strlen(cmdLine) > 4)
            rc = -2; // Syntax error.
          else
            strcpy(command, cmdLine);
          iCL = 0;
        }
      }
    if (rc > 0) {
      for(uint8_t i = 0 ; i < strlen(command); i ++)
        command[ i ] = toupper(command[ i ]);
    } else if (rc == -2) {
      iCL = 0;
      client.println("500 Syntax error");
    }
  }
  return rc;
}
