#include <cassert>
#include <sstream>

#include <resip/stack/Symbols.hxx>
#include <resip/stack/Tuple.hxx>
#include <rutil/Data.hxx>
#include <rutil/DnsUtil.hxx>
#include <rutil/Logger.hxx>
#include <rutil/ParseBuffer.hxx>
#include <rutil/Socket.hxx>
#include <rutil/TransportType.hxx>

#include "repro/RegSyncClient.hxx"

using namespace repro;
using namespace resip;
using namespace std;

#define RESIPROCATE_SUBSYSTEM Subsystem::REPRO

RegSyncClient::RegSyncClient(InMemorySyncRegDb* regDb,
                             Data address,
                             unsigned short port) :
   mRegDb(regDb),
   mAddress(address),
   mPort(port),
   mSocketDesc(0)
{
    assert(mRegDb);
}

void 
RegSyncClient::delaySeconds(unsigned int seconds)
{
   // Delay for requested number of seconds - but check every second if we are shutdown or not
   for(unsigned int i = 0; i < seconds && !mShutdown; i++)
   {
#ifdef WIN32
      Sleep(1000);
#else
      sleep(1);
#endif
   }
}

void
RegSyncClient::shutdown()
{
    ThreadIf::shutdown();
    if(mSocketDesc) closeSocket(mSocketDesc);
    mSocketDesc = 0;
}

void 
RegSyncClient::thread()
{
   int rc;
   struct sockaddr_in localAddr, servAddr;
   struct hostent *h;

   h = gethostbyname(mAddress.c_str());
   if(h==0) 
   {
      ErrLog(<< "RegSyncClient: unknown host " << mAddress);
      return;
   }

   servAddr.sin_family = h->h_addrtype;
   memcpy((char *) &servAddr.sin_addr.s_addr, h->h_addr_list[0], h->h_length);
   servAddr.sin_port = htons(mPort);

   while(!mShutdown)
   {
      // Create TCP Socket
      mSocketDesc = socket(servAddr.sin_family, SOCK_STREAM, 0);
      if(mSocketDesc < 0) 
      {
         ErrLog(<< "RegSyncClient: cannot open socket");
         mSocketDesc = 0;
         return;
      }

      // bind to any local interface/port
      localAddr.sin_family = servAddr.sin_family;
      localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
      localAddr.sin_port = 0;

      rc = bind(mSocketDesc, (struct sockaddr *) &localAddr, sizeof(localAddr));
      if(rc < 0) 
      {
         ErrLog(<<"RegSyncClient: error binding locally");
         closeSocket(mSocketDesc);
         mSocketDesc = 0;
         return;
      }

      // Connect to server
      rc = connect(mSocketDesc, (struct sockaddr *) &servAddr, sizeof(servAddr));
      if(rc < 0) 
      {
         if(!mShutdown) ErrLog(<< "RegSyncClient: error connecting to " << mAddress << ":" << mPort);
         if(mSocketDesc)
         {
            closeSocket(mSocketDesc);
            mSocketDesc = 0;
         }
         delaySeconds(30);
         continue;
      }

      Data request(
         "<InitialSync>\r\n"
         "  <Request>\r\n"
         "     <Version>1</Version>\r\n"   // For future use in detecting if client/server are a compatible version
         "  </Request>\r\n"
         "</InitialSync>\r\n");   
      rc = send(mSocketDesc, request.c_str(), request.size(), 0);
      if(rc < 0) 
      {
         if(!mShutdown) ErrLog(<< "RegSyncClient: error sending");
         if(mSocketDesc)
         {
            closeSocket(mSocketDesc);
            mSocketDesc = 0;
         }
         continue;
      }

      while(rc > 0)
      {
         rc = recv(mSocketDesc, (char*)&mRxBuffer, sizeof(mRxBuffer), 0);
         if(rc < 0) 
         {
            if(!mShutdown) ErrLog(<< "RegSyncClient: error receiving");
            if(mSocketDesc)
            {
               closeSocket(mSocketDesc);
               mSocketDesc = 0;
            }
            break;
         }

         if(rc > 0)
         {
            mRxDataBuffer += Data(Data::Borrow, (const char*)&mRxBuffer, rc);   
            while(tryParse());
         }
      }
   } // end while

   if(mSocketDesc) closeSocket(mSocketDesc);
}

bool 
RegSyncClient::tryParse()
{
   ParseBuffer pb(mRxDataBuffer);
   Data initialTag;
   const char* start = pb.position();
   pb.skipWhitespace();
   pb.skipToChar('<');   
   if(!pb.eof())
   {
      pb.skipChar();
      const char* anchor = pb.position();
      pb.skipToChar('>');
      if(!pb.eof())
      {
         initialTag = pb.data(anchor);
         // Find end of initial tag
         pb.skipToChars("</" + initialTag + ">");
         if (!pb.eof())
         {
            pb.skipN(initialTag.size() + 3);  // Skip past </InitialTag>            
            handleXml(pb.data(start));

            // Remove processed data from RxBuffer
            pb.skipWhitespace();
            if(!pb.eof())
            {
               anchor = pb.position();
               pb.skipToEnd();
               mRxDataBuffer = pb.data(anchor);
               return true;
            }
            else
            {
               mRxDataBuffer.clear();
            }
         }   
      }
   }
   return false;
}

void 
RegSyncClient::handleXml(const Data& xmlData)
{
   //InfoLog(<< "RegSyncClient::handleXml received: " << xmlData);

   try
   {
      ParseBuffer pb(xmlData);
      XMLCursor xml(pb);

      if(isEqualNoCase(xml.getTag(), "InitialSync"))
      {
         // Must be an InitialSync response
         InfoLog(<< "RegSyncClient::handleXml: InitialSync complete.");
      }
      else if(isEqualNoCase(xml.getTag(), "reginfo"))
      {
         try
         {
            handleRegInfoEvent(xml);
         }
         catch(BaseException& e)
         {
             ErrLog(<< "RegSyncClient::handleXml: exception: " << e);
         }
      }
      else 
      {
         WarningLog(<< "RegSyncClient::handleXml: Ignoring XML message with unknown method: " << xml.getTag());
      }
   }
   catch(resip::BaseException& e)
   {
      WarningLog(<< "RegSyncClient::handleXml: Ignoring XML message due to ParseException: " << e);
   }
}

void 
RegSyncClient::handleRegInfoEvent(resip::XMLCursor& xml)
{
   DebugLog(<< "RegSyncClient::handleRegInfoEvent");
   Uri aor;
   ContactList contacts;
   if(xml.firstChild())
   {
      do
      {
          if(isEqualNoCase(xml.getTag(), "aor"))
          {
             if(xml.firstChild())
             {
                aor = Uri(xml.getValue().xmlCharDataDecode());
                xml.parent();
             }
             //InfoLog(<< "RegSyncClient::handleRegInfoEvent: aor=" << aor);
          }
          else if(isEqualNoCase(xml.getTag(), "contactinfo"))
          {
              if(xml.firstChild())
              {
                  ContactInstanceRecord rec;
                  do
                  {
                     if(isEqualNoCase(xml.getTag(), "contacturi"))
                     {
                        if(xml.firstChild())
                        {
                           //InfoLog(<< "RegSyncClient::handleRegInfoEvent: contacturi=" << xml.getValue());
                           rec.mContact = NameAddr(xml.getValue().xmlCharDataDecode());
                           xml.parent();
                        }
                     }
                     else if(isEqualNoCase(xml.getTag(), "expires"))
                     {
                        if(xml.firstChild())
                        {
                           //InfoLog(<< "RegSyncClient::handleRegInfoEvent: expires=" << xml.getValue());
                           rec.mRegExpires = xml.getValue().convertUInt64();
                           xml.parent();
                        }
                     }
                     else if(isEqualNoCase(xml.getTag(), "lastupdate"))
                     {
                        if(xml.firstChild())
                        {
                           //InfoLog(<< "RegSyncClient::handleRegInfoEvent: lastupdate=" << xml.getValue());
                           rec.mLastUpdated = xml.getValue().convertUInt64();
                           xml.parent();
                        }
                     }
                     else if(isEqualNoCase(xml.getTag(), "receivedfrom"))
                     {
                        if(xml.firstChild())
                        {
                           rec.mReceivedFrom = Tuple::makeTuple(xml.getValue().base64decode());
                           //InfoLog(<< "RegSyncClient::handleRegInfoEvent: receivedfrom=" << xml.getValue() << " tuple=" << rec.mReceivedFrom);
                           xml.parent();
                        }
                     }
                     else if(isEqualNoCase(xml.getTag(), "sippath"))
                     {
                        if(xml.firstChild())
                        {
                           //InfoLog(<< "RegSyncClient::handleRegInfoEvent: sippath=" << xml.getValue());
                           rec.mSipPath.push_back(NameAddr(xml.getValue().xmlCharDataDecode()));
                           xml.parent();
                        }
                     }
                     else if(isEqualNoCase(xml.getTag(), "instance"))
                     {
                        if(xml.firstChild())
                        {
                           //InfoLog(<< "RegSyncClient::handleRegInfoEvent: instance=" << xml.getValue());
                           rec.mInstance = xml.getValue().xmlCharDataDecode();
                           xml.parent();
                        }
                     }
                     else if(isEqualNoCase(xml.getTag(), "regid"))
                     {
                        if(xml.firstChild())
                        {
                           //InfoLog(<< "RegSyncClient::handleRegInfoEvent: regid=" << xml.getValue());
                           rec.mRegId = xml.getValue().convertUnsignedLong();
                           xml.parent();
                        }
                     }
                  } while(xml.nextSibling());
                  xml.parent();

                  // Add record to list
                  rec.mSyncContact = true;  // This ContactInstanceRecord came from registration sync process
                  contacts.push_back(rec);
              }
          }
      } while(xml.nextSibling());
      xml.parent();
   }
   xml.parent();

   processModify(aor, contacts);
}

void 
RegSyncClient::processModify(const resip::Uri& aor, ContactList& syncContacts)
{
   ContactList currentContacts;

   mRegDb->lockRecord(aor);
   mRegDb->getContacts(aor, currentContacts);

   InfoLog(<< "RegSyncClient::processModify: for aor=" << aor << 
              ", numSyncContacts=" << syncContacts.size() << 
              ", numCurrentContacts=" << currentContacts.size());

   // Iteratate through new syncContact List
   ContactList::iterator itSync = syncContacts.begin();
   ContactList::iterator itCurrent;
   bool found;
   for(; itSync != syncContacts.end(); itSync++)
   {
       // See if contact already exists in currentContacts       
       found = false;
       for(itCurrent = currentContacts.begin(); itCurrent != currentContacts.end(); itCurrent++)
       {
           if(*itSync == *itCurrent)
           {
               found = true;
               // We found a match - check if sycnContacts LastUpdated time is newer
               if(itSync->mLastUpdated > itCurrent->mLastUpdated)
               {
                   // Replace current contact with Sync contact
                   mRegDb->updateContact(aor, *itSync);
               }
           }
       }
       if(!found)
       {
           mRegDb->updateContact(aor, *itSync);
       }
   }
   mRegDb->unlockRecord(aor);
}

/* ====================================================================
 * The Vovida Software License, Version 1.0 
 * 
 * Copyright (c) 2000 Vovida Networks, Inc.  All rights reserved.
 * Copyright (c) 2010 SIP Spectrum, Inc.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 
 * 3. The names "VOCAL", "Vovida Open Communication Application Library",
 *    and "Vovida Open Communication Application Library (VOCAL)" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact vocal@vovida.org.
 *
 * 4. Products derived from this software may not be called "VOCAL", nor
 *    may "VOCAL" appear in their name, without prior written
 *    permission of Vovida Networks, Inc.
 * 
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL VOVIDA
 * NETWORKS, INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT DAMAGES
 * IN EXCESS OF $1,000, NOR FOR ANY INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * 
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by Vovida
 * Networks, Inc. and many individuals on behalf of Vovida Networks,
 * Inc.  For more information on Vovida Networks, Inc., please see
 * <http://www.vovida.org/>.
 *
 */

