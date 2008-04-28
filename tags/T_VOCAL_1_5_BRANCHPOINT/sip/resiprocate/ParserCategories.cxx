#include <algorithm>
#include <cassert>
#include <iostream>

#include "sip2/util/Data.hxx"
#include "sip2/util/Logger.hxx"
#include "sip2/util/ParseBuffer.hxx"
#include "sip2/sipstack/ParserCategories.hxx"
#include "sip2/sipstack/Uri.hxx"
#include "sip2/sipstack/UnknownParameter.hxx"

using namespace Vocal2;
using namespace std;

#define VOCAL_SUBSYSTEM Subsystem::SIP

Data Vocal2::DayOfWeekData[] =
{
   "Sun",
   "Mon",
   "Tue",
   "Wed",
   "Thu",
   "Fri",
   "Sat"
};

Data Vocal2::MonthData[] =
{
   "Jan",
   "Feb",
   "Mar",
   "Apr",
   "May",
   "Jun",
   "Jul",
   "Aug",
   "Sep",
   "Oct",
   "Nov",
   "Dec"
};

//====================
// Token
//===================
Token::Token(const Token& rhs)
   : ParserCategory(rhs),
     mValue(rhs.mValue)
{}

Token&
Token::operator=(const Token& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      mValue = rhs.mValue;
   }
   return *this;
}

bool
Token::operator<(const Token& rhs) const
{
   return (value() < rhs.value());
}

void
Token::parse(ParseBuffer& pb)
{
   const char* startMark = pb.skipWhitespace();
   pb.skipToOneOf(ParseBuffer::Whitespace, Symbols::SEMI_COLON);
   pb.data(mValue, startMark);
   pb.skipToChar(Symbols::SEMI_COLON[0]);
   parseParameters(pb);
}

ParserCategory* 
Token::clone() const
{
   return new Token(*this);
}

std::ostream& 
Token::encodeParsed(std::ostream& str) const
{
   str << mValue;
   encodeParameters(str);
   return str;
}

//====================
// MIME
//====================
Mime::Mime(const Data& type, const Data& subType) 
   : ParserCategory(), 
     mType(type), 
     mSubType(subType) 
{}

Mime::Mime(const Mime& rhs)
   : ParserCategory(rhs),
     mType(rhs.mType),
     mSubType(rhs.mSubType)
{}

Mime&
Mime::operator=(const Mime& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      mType = rhs.mType;
      mSubType = rhs.mSubType;
   }
   return *this;
}

bool
Mime::operator<(const Mime& rhs) const
{
   if (type() < rhs.type())
   {
      return true;
   }
   else if (type() > rhs.type())
   {
      return false;
   }
   return subType() < rhs.subType();
}

void
Mime::parse(ParseBuffer& pb)
{
   const char* anchor = pb.skipWhitespace();

   pb.skipToOneOf(ParseBuffer::Whitespace, Symbols::SLASH);
   pb.data(mType, anchor);

   pb.skipWhitespace();
   pb.skipChar(Symbols::SLASH[0]);

   anchor = pb.skipWhitespace();
   pb.skipToOneOf(ParseBuffer::Whitespace, Symbols::SEMI_COLON);
   pb.data(mSubType, anchor);

   //   pb.skipWhitespace(); -- This is really a bad line - don't do it - it eat
   //   the CRLF before the body of a message 

   parseParameters(pb);
}

ParserCategory* 
Mime::clone() const
{
   return new Mime(*this);
}

std::ostream&
Mime::encodeParsed(std::ostream& str) const
{
   str << mType << Symbols::SLASH << mSubType ;
   encodeParameters(str);
   return str;
}

//====================
// Auth:
//====================
Auth::Auth(const Auth& rhs)
   : ParserCategory(rhs)
{
   if (isParsed())
   {
      scheme() = rhs.scheme();
   }
}


Auth&
Auth::operator=(const Auth& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      scheme() = rhs.scheme();
   }
   return *this;
}


void
Auth::parse(ParseBuffer& pb)
{
   const char* start;
   start = pb.skipWhitespace();
   pb.skipToOneOf(ParseBuffer::Whitespace, Symbols::EQUALS);

   if (*pb.position() == Symbols::EQUALS[0])
   {
      // Authentication-Info only
      // back up, and then parse
      pb.reset(start);
      parseAuthParameters(pb);
   }
   else
   {
      // everything else
      pb.data(mScheme, start);

      pb.skipWhitespace();
      parseAuthParameters(pb);
   }
}

std::ostream& 
Auth::encodeParsed(std::ostream& str) const
{
   if (!mScheme.empty())
   {
      str << mScheme << Symbols::SPACE;
   }

   encodeAuthParameters(str);

   return str;
}

ParserCategory* 
Auth::clone() const
{
   return new Auth(*this);
}

void
Auth::parseAuthParameters(ParseBuffer& pb)
{
   while (!pb.eof())
   {
      const char* keyStart = pb.position();
      const char* keyEnd = pb.skipToOneOf(" \t\r\n=");
      ParameterTypes::Type type = ParameterTypes::getType(keyStart, (keyEnd - keyStart));
      if (type == ParameterTypes::UNKNOWN)
      {
         mUnknownParameters.push_back(new UnknownParameter(keyStart, int((keyEnd - keyStart)), pb));
      }
      else
      {
         // invoke the particular factory
         mParameters.push_back(ParameterTypes::ParameterFactories[type](type, pb, " \t\r\n,"));
      }
      pb.skipWhitespace();
      if (*pb.position() != Symbols::COMMA[0])
      {
	 break;
      }
      pb.skipChar();
      pb.skipWhitespace();
   }
}      

ostream&
Auth::encodeAuthParameters(ostream& str) const
{
   bool first = true;
   for (ParameterList::iterator it = mParameters.begin();
        it != mParameters.end(); it++)
   {
      if (!first)
      {
         str << Symbols::COMMA;
      }
      first = false;
      (*it)->encode(str);
   }

   for (ParameterList::iterator it = mUnknownParameters.begin();
        it != mUnknownParameters.end(); it++)
   {
      if (!first)
      {
         str << Symbols::COMMA;
      }
      first = false;
      (*it)->encode(str);
   }
   return str;
}

      
Algorithm_Param::DType&
Auth::param(const Algorithm_Param& paramType) const
{
   checkParsed();
   Algorithm_Param::Type* p = dynamic_cast<Algorithm_Param::Type*>(getParameterByEnum(paramType.getTypeNum()));
   if (!p)
   {
      p = new Algorithm_Param::Type(paramType.getTypeNum());
      mParameters.push_back(p);
   }
   return p->value();
}

Cnonce_Param::DType&
Auth::param(const Cnonce_Param& paramType) const
{
   checkParsed();
   Cnonce_Param::Type* p = dynamic_cast<Cnonce_Param::Type*>(getParameterByEnum(paramType.getTypeNum()));
   if (!p)
   {
      p = new Cnonce_Param::Type(paramType.getTypeNum());
      mParameters.push_back(p);
   }
   return p->value();
}

Nonce_Param::DType&
Auth::param(const Nonce_Param& paramType) const
{
   checkParsed();
   Nonce_Param::Type* p = dynamic_cast<Nonce_Param::Type*>(getParameterByEnum(paramType.getTypeNum()));
   if (!p)
   {
      p = new Nonce_Param::Type(paramType.getTypeNum());
      mParameters.push_back(p);
   }
   return p->value();
}

Domain_Param::DType&
Auth::param(const Domain_Param& paramType) const
{
   checkParsed();
   Domain_Param::Type* p = dynamic_cast<Domain_Param::Type*>(getParameterByEnum(paramType.getTypeNum()));
   if (!p)
   {
      p = new Domain_Param::Type(paramType.getTypeNum());
      mParameters.push_back(p);
   }
   return p->value();
}

Nc_Param::DType&
Auth::param(const Nc_Param& paramType) const
{
   checkParsed();
   Nc_Param::Type* p = dynamic_cast<Nc_Param::Type*>(getParameterByEnum(paramType.getTypeNum()));
   if (!p)
   {
      p = new Nc_Param::Type(paramType.getTypeNum());
      mParameters.push_back(p);
   }
   return p->value();
}

Opaque_Param::DType&
Auth::param(const Opaque_Param& paramType) const
{
   checkParsed();
   Opaque_Param::Type* p = dynamic_cast<Opaque_Param::Type*>(getParameterByEnum(paramType.getTypeNum()));
   if (!p)
   {
      p = new Opaque_Param::Type(paramType.getTypeNum());
      mParameters.push_back(p);
   }
   return p->value();
}

Realm_Param::DType&
Auth::param(const Realm_Param& paramType) const
{
   checkParsed();
   Realm_Param::Type* p = dynamic_cast<Realm_Param::Type*>(getParameterByEnum(paramType.getTypeNum()));
   if (!p)
   {
      p = new Realm_Param::Type(paramType.getTypeNum());
      mParameters.push_back(p);
   }
   return p->value();
}

Response_Param::DType&
Auth::param(const Response_Param& paramType) const
{
   checkParsed();
   Response_Param::Type* p = dynamic_cast<Response_Param::Type*>(getParameterByEnum(paramType.getTypeNum()));
   if (!p)
   {
      p = new Response_Param::Type(paramType.getTypeNum());
      mParameters.push_back(p);
   }
   return p->value();
}

Stale_Param::DType&
Auth::param(const Stale_Param& paramType) const
{
   checkParsed();
   Stale_Param::Type* p = dynamic_cast<Stale_Param::Type*>(getParameterByEnum(paramType.getTypeNum()));
   if (!p)
   {
      p = new Stale_Param::Type(paramType.getTypeNum());
      mParameters.push_back(p);
   }
   return p->value();
}

Username_Param::DType&
Auth::param(const Username_Param& paramType) const
{
   checkParsed();
   Username_Param::Type* p = dynamic_cast<Username_Param::Type*>(getParameterByEnum(paramType.getTypeNum()));
   if (!p)
   {
      p = new Username_Param::Type(paramType.getTypeNum());
      mParameters.push_back(p);
   }
   return p->value();
}

Uri_Param::DType&
Auth::param(const Uri_Param& paramType) const
{
   checkParsed();
   Uri_Param::Type* p = dynamic_cast<Uri_Param::Type*>(getParameterByEnum(paramType.getTypeNum()));
   if (!p)
   {
      p = new Uri_Param::Type(paramType.getTypeNum());
      mParameters.push_back(p);
   }
   return p->value();
}

Qop_Options_Param::DType&
Auth::param(const Qop_Options_Param& paramType) const
{
   checkParsed();
   Qop_Options_Param::Type* p = dynamic_cast<Qop_Options_Param::Type*>(getParameterByEnum(paramType.getTypeNum()));
   if (!p)
   {
      p = new Qop_Options_Param::Type(paramType.getTypeNum());
      mParameters.push_back(p);
   }
   return p->value();
}

Qop_Param::DType&
Auth::param(const Qop_Param& paramType) const
{
   checkParsed();
   Qop_Param::Type* p = dynamic_cast<Qop_Param::Type*>(getParameterByEnum(paramType.getTypeNum()));
   if (!p)
   {
      p = new Qop_Param::Type(paramType.getTypeNum());
      mParameters.push_back(p);
   }
   return p->value();
}

//====================
// CSeqCategory:
//====================
CSeqCategory::CSeqCategory(const CSeqCategory& rhs)
   : ParserCategory(rhs),
     mMethod(rhs.mMethod),
     mSequence(rhs.mSequence)
{}

CSeqCategory::CSeqCategory() 
   : ParserCategory(), 
     mMethod(UNKNOWN), 
     mUnknownMethodName(MethodNames[UNKNOWN]),
     mSequence(-1) 
{}

CSeqCategory&
CSeqCategory::operator=(const CSeqCategory& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      mMethod = rhs.mMethod;
      mSequence = rhs.mSequence;
   }
   return *this;
}
ParserCategory* 
CSeqCategory::clone() const
{
   return new CSeqCategory(*this);
}

// examples to test: 
// "CSeq:15 ACK"  // ok
// "CSeq:ACK"     // bad
// "CSeq:JOE"     // ok
// "CSeq:1 JOE"   // ok
// "CSeq:1323333 INVITE" // ok 
// "CSeq:1323333 Invite" // ok - not invite
// "CSeq:1323333 InviTe" // ok - not invite
// "CSeq:\t\t  \t15\t\t\t    \t ACK"  // ok
// "CSeq:\t\t  \t15\t\t\t    \t"  // bad
// "CSeq:1xihzihsihtqnognsd INVITE" // not ok, but parses (?)

void
CSeqCategory::parse(ParseBuffer& pb)
{
   const char* anchorPtr;
   //      anchorPtr = 
   pb.skipWhitespace();
   mSequence = pb.integer();

   pb.skipNonWhitespace();
   anchorPtr = pb.skipWhitespace();
   pb.skipNonWhitespace(); // .dcm. maybe pass an arg that says throw if you
                           // don't move
   mMethod = getMethodType(anchorPtr, pb.position() - anchorPtr);
   if (mMethod == UNKNOWN)
   {
      pb.data(mUnknownMethodName, anchorPtr);
   }
}

std::ostream& 
CSeqCategory::encodeParsed(std::ostream& str) const
{
   str << mSequence << Symbols::SPACE << (mMethod != UNKNOWN ? MethodNames[mMethod] : mUnknownMethodName);
   return str;
}

//====================
// Date
//====================
DateCategory::DateCategory(const DateCategory& rhs)
   : ParserCategory(rhs),
     mDayOfWeek(Sun),
     mDayOfMonth(),
     mMonth(Jan),
     mYear(0),
     mHour(0),
     mMin(0),
     mSec(0)
{}

DateCategory&
DateCategory::operator=(const DateCategory& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      mDayOfWeek = rhs.mDayOfWeek;
      mDayOfMonth = rhs.mDayOfMonth;
      mMonth = rhs.mMonth;
      mYear = rhs.mYear;
      mHour = rhs.mHour;
      mMin = rhs.mMin;
      mSec = rhs.mSec;
   }
   return *this;
}

/* ANSI-C code produced by gperf version 2.7.2 */
/* Command-line: gperf -L ANSI-C -t -k '*' dayofweek.gperf  */
struct days { char *name; DayOfWeek day; };

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static unsigned int
dayofweek_hash (register const char *str, register unsigned int len)
{
  static unsigned char asso_values[] =
    {
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
       1, 13, 13, 13, 13, 13, 13,  5, 13, 13,
      13, 13, 13,  2,  0, 13, 13,  7, 13, 13,
      13, 13, 13, 13, 13, 13, 13,  7, 13, 13,
       0,  0, 13, 13,  4,  0, 13, 13, 13, 13,
       0,  0, 13, 13,  0, 13,  0,  0, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
      13, 13, 13, 13, 13, 13
    };
  register int hval = len;

  switch (hval)
    {
      default:
      case 3:
        hval += asso_values[(unsigned char)str[2]];
      case 2:
        hval += asso_values[(unsigned char)str[1]];
      case 1:
        hval += asso_values[(unsigned char)str[0]];
        break;
    }
  return hval;
}

#ifdef __GNUC__
__inline
#endif
struct days *
in_dayofweek_word_set (register const char *str, register unsigned int len)
{
   static const unsigned int MIN_WORD_LENGTH = 3;
   static const unsigned int MAX_WORD_LENGTH = 3;
   static const int MAX_HASH_VALUE = 12;
   
   static struct days wordlist[] =
    {
      {""}, {""}, {""},
      {"Tue", Tue},
      {"Fri", Fri},
      {"Sun", Sun},
      {""},
      {"Thu", Thu},
      {"Mon", Mon},
      {""},
      {"Wed", Wed},
      {""},
      {"Sat", Sat}
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register int key = dayofweek_hash (str, len);

      if (key <= MAX_HASH_VALUE && key >= 0)
        {
          register const char *s = wordlist[key].name;

          if (*str == *s && !strcmp (str + 1, s + 1))
            return &wordlist[key];
        }
    }
  return 0;
}

DayOfWeek
DateCategory::DayOfWeekFromData(const Data& dow)
{
   static const unsigned int MIN_WORD_LENGTH = 3;
   static const unsigned int MAX_WORD_LENGTH = 3;
   static const int MAX_HASH_VALUE = 12;

   register const char *str = dow.data();
   register unsigned int len = dow.size();

   static struct days wordlist[] =
      {
         {""}, {""}, {""},
         {"Tue", Tue},
         {"Fri", Fri},
         {"Sun", Sun},
         {""},
         {"Thu", Thu},
         {"Mon", Mon},
         {""},
         {"Wed", Wed},
         {""},
         {"Sat", Sat}
      };

   if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
   {
      register int key = dayofweek_hash (str, len);
      
      if (key <= MAX_HASH_VALUE && key >= 0)
      {
         register const char *s = wordlist[key].name;
         
         if (*str == *s && !strncmp (str+1, s+1, len-1))
         {
            return wordlist[key].day;
         }
      }
   }
   return Sun;
}

/* ANSI-C code produced by gperf version 2.7.2 */
/* Command-line: gperf -L ANSI-C -t -k '*' month.gperf  */
struct months { char *name; Month type; };

/* maximum key range = 31, duplicates = 0 */

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static unsigned int
month_hash (register const char *str, register unsigned int len)
{
   static unsigned char asso_values[] =
      {
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 15, 34, 34,  8, 34,
         5, 34, 34, 34,  0, 34, 34, 10,  3, 14,
         34, 34, 34,  9, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 10,  0,  0,
         34,  0, 34, 10, 34, 34, 34, 34,  4, 34,
         0,  0,  0, 34,  0, 34,  0,  0,  0, 34,
         34, 10, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34, 34, 34, 34, 34,
         34, 34, 34, 34, 34, 34
      };
   register int hval = len;

   switch (hval)
   {
      default:
      case 3:
         hval += asso_values[(unsigned char)str[2]];
      case 2:
         hval += asso_values[(unsigned char)str[1]];
      case 1:
         hval += asso_values[(unsigned char)str[0]];
         break;
   }
   return hval;
}

Month
DateCategory::MonthFromData(const Data& mon)
{
   static const unsigned int MIN_WORD_LENGTH = 3;
   static const unsigned int MAX_WORD_LENGTH = 3;
   static const int MAX_HASH_VALUE = 33;

   register const char *str = mon.data();
   register unsigned int len = mon.size();

   static struct months wordlist[] =
      {
         {""}, {""}, {""},
         {"Jun", Jun},
         {""}, {""},
         {"Nov", Nov},
         {"Jul", Jul},
         {"Feb", Feb},
         {""}, {""},
         {"Dec", Dec},
         {"Sep", Sep},
         {"Jan", Jan},
         {""}, {""}, {""},
         {"Oct", Oct},
         {"Apr", Apr},
         {""}, {""}, {""}, {""},
         {"Mar", Mar},
         {""}, {""}, {""}, {""},
         {"Aug", Aug},
         {""}, {""}, {""}, {""},
         {"May", May}
      };

   if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
   {
      register int key = month_hash (str, len);

      if (key <= MAX_HASH_VALUE && key >= 0)
      {
         register const char *s = wordlist[key].name;

         if (*str == *s && !strncmp (str + 1, s + 1, mon.size()-1))
            return wordlist[key].type;
      }
   }
   return Jan;
}

void
DateCategory::parse(ParseBuffer& pb)
{
   // Mon, 04 Nov 2002 17:34:15 GMT

   const char* anchor = pb.skipWhitespace();

   pb.skipToChar(Symbols::COMMA[0]);
   Data dayOfWeek;
   pb.data(dayOfWeek, anchor);
   mDayOfWeek = DateCategory::DayOfWeekFromData(dayOfWeek);

   pb.skipChar(Symbols::COMMA[0]);

   pb.skipWhitespace();

   mDayOfMonth = pb.integer();

   anchor = pb.skipWhitespace();
   pb.skipNonWhitespace();

   Data month;
   pb.data(month, anchor);
   mMonth = DateCategory::MonthFromData(month);

   pb.skipWhitespace();
   mYear = pb.integer();

   pb.skipWhitespace();

   mHour = pb.integer();
   pb.skipChar(Symbols::COLON[0]);
   mMin = pb.integer();
   pb.skipChar(Symbols::COLON[0]);
   mSec = pb.integer();

   pb.skipWhitespace();
   pb.skipChar('G');
   pb.skipChar('M');
   pb.skipChar('T');

   pb.skipWhitespace();
   pb.assertEof();
}

ParserCategory* 
DateCategory::clone() const
{
   return new DateCategory(*this);
}

static void pad2(const int x, std::ostream& str)
{
   if (x < 10)
   {
      str << Symbols::ZERO[0];
   }
   str << x;
}

std::ostream& 
DateCategory::encodeParsed(std::ostream& str) const
{
   str << DayOfWeekData[mDayOfWeek] // Mon
       << Symbols::COMMA[0] << Symbols::SPACE[0];
   
   pad2(mDayOfMonth, str);  //  04

   str << Symbols::SPACE[0]
       << MonthData[mMonth] << Symbols::SPACE[0] // Nov
       << mYear << Symbols::SPACE[0]; // 2002

   pad2(mHour, str);
   str << Symbols::COLON[0];
   pad2(mMin, str);
   str << Symbols::COLON[0];
   pad2(mSec, str);
   str << " GMT";

   return str;
}

//====================
// WarningCategory
//====================
WarningCategory::WarningCategory(const WarningCategory& rhs)
   : ParserCategory(rhs)
{}

WarningCategory&
WarningCategory::operator=(const WarningCategory& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      mCode = rhs.mCode;
      mHostname = rhs.mHostname;
      mText = rhs.mText;
   }
   return *this;
}
void
WarningCategory::parse(ParseBuffer& pb)
{
   pb.skipWhitespace();
   mCode = pb.integer();

   const char* anchor = pb.skipWhitespace();
   pb.skipNonWhitespace();
   pb.data(mHostname, anchor);

   pb.skipWhitespace();
   anchor = pb.skipChar(Symbols::DOUBLE_QUOTE[0]);
   pb.skipToEndQuote(Symbols::DOUBLE_QUOTE[0]);
   pb.data(mText, anchor);
   anchor = pb.skipChar(Symbols::DOUBLE_QUOTE[0]);
   pb.skipWhitespace();
   pb.assertEof();
}

ParserCategory* 
WarningCategory::clone() const
{
   return new WarningCategory(*this);
}

int&
WarningCategory::code()
{
   checkParsed();
   return mCode;
}

Data&
WarningCategory::hostname()
{
   checkParsed();
   return mHostname;
}

Data&
WarningCategory::text()
{
   checkParsed();
   return mText;
}

std::ostream& 
WarningCategory::encodeParsed(std::ostream& str) const
{
   str << mCode << Symbols::SPACE[0];
   str << mHostname << Symbols::SPACE[0];
   str << Symbols::DOUBLE_QUOTE[0] << mText << Symbols::DOUBLE_QUOTE[0];

   return str;
}

//====================
// Integer:
//====================
IntegerCategory::IntegerCategory(const IntegerCategory& rhs)
   : ParserCategory(rhs),
     mValue(rhs.mValue),
     mComment(rhs.mComment)
{}

IntegerCategory&
IntegerCategory::operator=(const IntegerCategory& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      mValue = rhs.mValue;
      mComment = rhs.mComment;
   }
   return *this;
}

ParserCategory* IntegerCategory::clone() const
{
   return new IntegerCategory(*this);
}

void
IntegerCategory::parse(ParseBuffer& pb)
{
   const char* start = pb.skipWhitespace();
   mValue = pb.integer();
   pb.skipToChar('(');
   if (!pb.eof())
   {
      start = pb.skipChar();
      pb.skipToEndQuote(')');
      pb.data(mComment, start);
      pb.skipChar();
   }
   else
   {
      pb.reset(start);
      start = pb.skipNonWhitespace();
   }
   
   parseParameters(pb);
}

std::ostream& 
IntegerCategory::encodeParsed(std::ostream& str) const
{
  str << mValue;

  if (!mComment.empty())
  {
     str << "(" << mComment << ")";
  }
  
  encodeParameters(str);
  return str;
}

//====================
// Expires:
//====================
ExpiresCategory::ExpiresCategory(const ExpiresCategory& rhs)
   : ParserCategory(rhs),
     mValue(rhs.mValue)
{}

ExpiresCategory&
ExpiresCategory::operator=(const ExpiresCategory& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      mValue = rhs.mValue;
   }
   return *this;
}

ParserCategory* ExpiresCategory::clone() const
{
   return new ExpiresCategory(*this);
}

void
ExpiresCategory::parse(ParseBuffer& pb)
{
   pb.skipWhitespace();
   const char *p = pb.position();
   if (!pb.eof() && isdigit(*p))
   {
     mValue = pb.integer();
   }
   else
   {
      mValue = 3600;
   }
   pb.skipToChar(Symbols::SEMI_COLON[0]);
   parseParameters(pb);
}

std::ostream& 
ExpiresCategory::encodeParsed(std::ostream& str) const
{
   str << mValue;
   encodeParameters(str);
   return str;
}

//====================
// StringCategory
//====================
StringCategory::StringCategory(const StringCategory& rhs)
   : ParserCategory(rhs),
     mValue(rhs.mValue)
{}

StringCategory&
StringCategory::operator=(const StringCategory& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      mValue = rhs.mValue;
   }
   return *this;
}
ParserCategory* 
StringCategory::clone() const
{
   return new StringCategory(*this);
}

void 
StringCategory::parse(ParseBuffer& pb)
{
   const char* anchor = pb.position();
   pb.skipToEnd();
   pb.data(mValue, anchor);
}

std::ostream& 
StringCategory::encodeParsed(std::ostream& str) const
{
   str << mValue;
   return str;
}

//====================
// GenericUri
//====================
GenericURI::GenericURI(const GenericURI& rhs)
   : ParserCategory(rhs)
{}

GenericURI&
GenericURI::operator=(const GenericURI& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      mUri = rhs.mUri;
   }
   return *this;
}


void
GenericURI::parse(ParseBuffer& pb)
{
   pb.skipWhitespace();
   const char* anchor = pb.skipChar(Symbols::LA_QUOTE[0]);

   pb.skipToChar(Symbols::RA_QUOTE[0]);
   pb.data(mUri, anchor);
   pb.skipChar(Symbols::RA_QUOTE[0]);

   pb.skipWhitespace();

   parseParameters(pb);
}


Data& GenericURI::uri()
{
   checkParsed();
   return mUri;
}

ParserCategory* 
GenericURI::clone() const
{
   return new GenericURI(*this);
}

std::ostream& 
GenericURI::encodeParsed(std::ostream& str) const
{
   str << Symbols::LA_QUOTE[0]
       << mUri
       << Symbols::RA_QUOTE[0];

   encodeParameters(str);

   return str;
}

//====================
// Via:
//====================
Via::Via(const Via& rhs)
   : ParserCategory(rhs),
     mProtocolName(rhs.mProtocolName),
     mProtocolVersion(rhs.mProtocolVersion),
     mTransport(rhs.mTransport),
     mSentHost(rhs.mSentHost),
     mSentPort(rhs.mSentPort)
{}

Via&
Via::operator=(const Via& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      mProtocolName = rhs.mProtocolName;
      mProtocolVersion = rhs.mProtocolVersion;
      mTransport = rhs.mTransport;
      mSentHost = rhs.mSentHost;
      mSentPort = rhs.mSentPort;
   }
   return *this;
}
ParserCategory *
Via::clone() const
{
   return new Via(*this);
}

void
Via::parse(ParseBuffer& pb)
{
   const char* startMark;
   startMark = pb.skipWhitespace();
   pb.skipToOneOf(ParseBuffer::Whitespace, Symbols::SLASH);
   pb.data(mProtocolName, startMark);
   pb.skipToChar('/');
   pb.skipChar();
   startMark = pb.skipWhitespace();
   pb.skipToOneOf(ParseBuffer::Whitespace, Symbols::SLASH);
   pb.data(mProtocolVersion, startMark);

   pb.skipToChar('/');
   pb.skipChar();
   startMark = pb.skipWhitespace();
   pb.skipNonWhitespace();
   pb.data(mTransport, startMark);

   startMark = pb.skipWhitespace();
   if (*startMark == '[')
   {
      pb.skipToChar(']');
      pb.skipChar();
   }
   else
   {
      pb.skipToOneOf(";:");
   }
   pb.data(mSentHost, startMark);
   pb.skipToOneOf(";:");
   if (*pb.position() == ':')
   {
      startMark = pb.skipChar(':');
      mSentPort = pb.integer();
      pb.skipToOneOf(ParseBuffer::Whitespace, Symbols::SEMI_COLON);
   }
   else
   {
      mSentPort = 0;
   }
   parseParameters(pb);
}

ostream&
Via::encodeParsed(ostream& str) const
{
   str << mProtocolName << Symbols::SLASH << mProtocolVersion << Symbols::SLASH << mTransport 
       << Symbols::SPACE << mSentHost;
   if (mSentPort != 0)
   {
      str << Symbols::COLON << mSentPort;
   }
   encodeParameters(str);
   return str;
}


//====================
// CallId:
//====================
CallId::CallId(const CallId& rhs)
   : ParserCategory(rhs),
     mValue(rhs.mValue)
{}

CallId&
CallId::operator=(const CallId& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      mValue = rhs.mValue;
   }
   return *this;
}

bool
CallId::operator==(const CallId& rhs) const
{
   return value() == rhs.value();
}

ParserCategory *
CallId::clone() const
{
   return new CallId(*this);
}

void
CallId::parse(ParseBuffer& pb)
{
   const char* start = pb.skipWhitespace();
   pb.skipToOneOf(ParseBuffer::Whitespace, Symbols::SEMI_COLON);
   pb.data(mValue, start);

   parseParameters(pb);
}

ostream&
CallId::encodeParsed(ostream& str) const
{
   str << mValue;
   encodeParameters(str);
   return str;
}

//====================
// NameAddr:
//====================
NameAddr::NameAddr(const NameAddr& rhs)
   : ParserCategory(rhs),
     mAllContacts(rhs.mAllContacts),
     mUri(rhs.mUri),
     mDisplayName(rhs.mDisplayName)
{}

NameAddr::NameAddr(const Data& unparsed)
   : ParserCategory(),
     mAllContacts(false),
     mDisplayName()
{
   ParseBuffer pb(unparsed.data(), unparsed.size());
   NameAddr tmp;
   tmp.parse(pb);
   *this = tmp;
}

NameAddr::NameAddr(const Uri& uri)
   : ParserCategory(),
     mAllContacts(false),
     mUri(uri),
     mDisplayName()
{}

NameAddr::~NameAddr()
{}

NameAddr&
NameAddr::operator=(const NameAddr& rhs)
{
   if (this != &rhs)
   {
      assert( &rhs != 0 );
      
      ParserCategory::operator=(rhs);
      mAllContacts = rhs.mAllContacts;
      mDisplayName = rhs.mDisplayName;
      mUri = rhs.mUri;
   }
   return *this;
}

bool
NameAddr::operator<(const NameAddr& rhs) const
{
   return mUri < rhs.mUri;
}

ParserCategory *
NameAddr::clone() const
{
   return new NameAddr(*this);
}

Uri&
NameAddr::uri() const 
{
   checkParsed(); 
   return mUri;
}

void
NameAddr::parse(ParseBuffer& pb)
{
   const char* start;
   start = pb.skipWhitespace();
   bool laQuote = false;
   if (*pb.position() == Symbols::DOUBLE_QUOTE[0])
   {
      pb.skipChar(Symbols::DOUBLE_QUOTE[0]);
      pb.skipToEndQuote();
      pb.skipChar(Symbols::DOUBLE_QUOTE[0]);
      pb.data(mDisplayName, start);
      laQuote = true;
      pb.skipToChar(Symbols::LA_QUOTE[0]);
      if (pb.eof())
      {
         throw ParseException("Expected '<'", __FILE__, __LINE__);
      }
      else
      {
         pb.skipChar(Symbols::LA_QUOTE[0]);
      }
   }
   else if (*pb.position() == Symbols::LA_QUOTE[0])
   {
      pb.skipChar(Symbols::LA_QUOTE[0]);
      laQuote = true;
   }
   else
   {
      start = pb.position();
      pb.skipToChar(Symbols::LA_QUOTE[0]);
      if (pb.eof())
      {
         pb.reset(start);
      }
      else
      {
         laQuote = true;
         pb.data(mDisplayName, start);
         pb.skipChar(Symbols::LA_QUOTE[0]);
      }
   }
   pb.skipWhitespace();
   mUri.parse(pb);
   if (laQuote)
   {
      pb.skipChar('>');
      pb.skipWhitespace();
      parseParameters(pb);
   }
   else
   {
      swap(mParameters, mUri.mParameters);
      swap(mUnknownParameters, mUri.mUnknownParameters);
      for (ParameterList::iterator it = mParameters.begin(); 
           it != mParameters.end();)
      {
         switch ((*it)->getType())
         {
            case ParameterTypes::transport:
            case ParameterTypes::ttl:
            case ParameterTypes::maddr:
            case ParameterTypes::lr:
            case ParameterTypes::method: 
            case ParameterTypes::comp:             
            {
               mUri.mParameters.push_back(*it);
               it = mParameters.erase(it);
               break;
            }
            default:
            {
               it++;
            }
         }
      }
   }
}

ostream&
NameAddr::encodeParsed(ostream& str) const
{
   //bool displayName = !mDisplayName.empty();
   str << mDisplayName << Symbols::LA_QUOTE;
   mUri.encodeParsed(str);
   str << Symbols::RA_QUOTE;
   
   encodeParameters(str);
   return str;
}

//====================
// RequestLine:
//====================
RequestLine::RequestLine(const RequestLine& rhs)
   : ParserCategory(rhs),
     mUri(rhs.mUri),
     mMethod(rhs.mMethod),
     mUnknownMethodName(rhs.mUnknownMethodName),
     mSipVersion(rhs.mSipVersion)
{}

RequestLine&
RequestLine::operator=(const RequestLine& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      mUri = rhs.mUri;
      mMethod = rhs.mMethod;
      mUnknownMethodName = rhs.mUnknownMethodName;
      mSipVersion = rhs.mSipVersion;
   }
   return *this;
}

RequestLine::~RequestLine()
{}

ParserCategory *
RequestLine::clone() const
{
   return new RequestLine(*this);
}

Uri&
RequestLine::uri() const 
{
   checkParsed(); 
   return mUri;
}

void 
RequestLine::parse(ParseBuffer& pb)
{
   const char* start;
   start = pb.skipWhitespace();
   pb.skipNonWhitespace();
   mMethod = getMethodType(start, pb.position() - start);
   if (mMethod == UNKNOWN)
   {
      pb.data(mUnknownMethodName, start);
   }
   pb.skipWhitespace();
   mUri.parse(pb);
   start = pb.skipWhitespace();
   pb.skipNonWhitespace();
   pb.data(mSipVersion, start);
}

ostream&
RequestLine::encodeParsed(ostream& str) const
{
   str << (mMethod != UNKNOWN ? MethodNames[mMethod] : mUnknownMethodName) << Symbols::SPACE;
   mUri.encodeParsed(str);
   str << Symbols::SPACE << mSipVersion;
   return str;
}

//====================
// StatusLine:
//====================
StatusLine::StatusLine(const StatusLine& rhs)
   : ParserCategory(rhs),
     mResponseCode(rhs.mResponseCode),
     mSipVersion(rhs.mSipVersion),
     mReason(rhs.mReason)
{}
     
StatusLine&
StatusLine::operator=(const StatusLine& rhs)
{
   if (this != &rhs)
   {
      ParserCategory::operator=(rhs);
      mResponseCode = rhs.mResponseCode;
      mSipVersion = rhs.mSipVersion;
      mReason = rhs.mReason;
   }
   return *this;
}

ParserCategory *
StatusLine::clone() const
{
   return new StatusLine(*this);
}

void
StatusLine::parse(ParseBuffer& pb)
{
   const char* start = pb.skipWhitespace();
   pb.skipNonWhitespace();
   pb.data(mSipVersion, start);

   start = pb.skipWhitespace();
   mResponseCode = pb.integer();
   start = pb.skipWhitespace();
   pb.reset(pb.end());
   pb.data(mReason, start);
}

ostream&
StatusLine::encodeParsed(ostream& str) const
{
   str << mSipVersion << Symbols::SPACE 
       << mResponseCode << Symbols::SPACE
       << mReason;
   return str;
}


/* ====================================================================
 * The Vovida Software License, Version 1.0 
 * 
 * Copyright (c) 2000 Vovida Networks, Inc.  All rights reserved.
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

/* Local Variables: */
/* c-file-style: "ellemtel" */
/* End: */
