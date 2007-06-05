/* vim:set ts=2 sw=2 et cindent: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla IPC.
 *
 * The Initial Developer of the Original Code is IBM Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2004
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Darin Fisher <darin@meer.net>
 *   Dmitry A. Kuminov <dmik@innotek.de>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "ipcDConnectService.h"
#include "ipcMessageWriter.h"
#include "ipcMessageReader.h"
#include "ipcLog.h"

#include "nsIServiceManagerUtils.h"
#include "nsIInterfaceInfo.h"
#include "nsIInterfaceInfoManager.h"
#include "nsIExceptionService.h"
#include "nsString.h"
#include "nsVoidArray.h"
#include "nsCRT.h"
#include "nsDeque.h"
#include "xptcall.h"

#if defined(DCONNECT_MULTITHREADED)

#include "nsIThread.h"
#include "nsIRunnable.h"

#if defined(DEBUG) && !defined(DCONNECT_STATS)
#define DCONNECT_STATS
#endif

#if defined(DCONNECT_STATS)
#include <stdio.h>
#endif

#endif

// XXX TODO:
//  1. add thread affinity field to SETUP messages
//  2. support array parameters

//-----------------------------------------------------------------------------

#define DCONNECT_IPC_TARGETID                      \
{ /* 43ca47ef-ebc8-47a2-9679-a4703218089f */       \
  0x43ca47ef,                                      \
  0xebc8,                                          \
  0x47a2,                                          \
  {0x96, 0x79, 0xa4, 0x70, 0x32, 0x18, 0x08, 0x9f} \
}
static const nsID kDConnectTargetID = DCONNECT_IPC_TARGETID;

//-----------------------------------------------------------------------------

#define DCON_WAIT_TIMEOUT PR_INTERVAL_NO_TIMEOUT

// used elsewhere like nsAtomTable to safely represent the integral value
// of an address.
typedef unsigned long PtrBits;

//-----------------------------------------------------------------------------

//
// +--------------------------------+
// | opcode : 1 byte                |
// +--------------------------------+
// | flags  : 1 byte                |
// +--------------------------------+
// .                                .
// . variable payload               .
// .                                .
// +--------------------------------+
//

// dconnect major opcodes
#define DCON_OP_SETUP   1
#define DCON_OP_RELEASE 2
#define DCON_OP_INVOKE  3

#define DCON_OP_SETUP_REPLY  4
#define DCON_OP_INVOKE_REPLY 5

// dconnect minor opcodes for DCON_OP_SETUP
#define DCON_OP_SETUP_NEW_INST_CLASSID    1
#define DCON_OP_SETUP_NEW_INST_CONTRACTID 2
#define DCON_OP_SETUP_GET_SERV_CLASSID    3
#define DCON_OP_SETUP_GET_SERV_CONTRACTID 4
#define DCON_OP_SETUP_QUERY_INTERFACE     5

// dconnect minor opcodes for RELEASE
// dconnect minor opcodes for INVOKE

struct DConnectOp
{
  PRUint8  opcode_major;
  PRUint8  opcode_minor;
  PRUint32 request_index; // initialized with NewRequestIndex
};

// SETUP structs

struct DConnectSetup : DConnectOp
{
  nsID iid;
};

struct DConnectSetupClassID : DConnectSetup
{
  nsID classid;
};

struct DConnectSetupContractID : DConnectSetup
{
  char contractid[1]; // variable length
};

struct DConnectSetupQueryInterface : DConnectSetup
{
  DConAddr instance;
};

// SETUP_REPLY struct

struct DConnectSetupReply : DConnectOp
{
  DConAddr instance;
  nsresult status;
  // followed by a specially serialized nsIException instance if
  // NS_FAILED(status) (see ipcDConnectService::SerializeException)
};

// RELEASE struct

struct DConnectRelease : DConnectOp
{
  DConAddr instance;
};

// INVOKE struct

struct DConnectInvoke : DConnectOp
{
  DConAddr instance;
  PRUint16 method_index;
  // followed by an array of in-param blobs
};

// INVOKE_REPLY struct

struct DConnectInvokeReply : DConnectOp
{
  nsresult result;
  // followed by an array of out-param blobs if NS_SUCCEEDED(result), or by a
  // specially serialized nsIException instance if NS_FAILED(result)
  // (see ipcDConnectService::SerializeException)
};

//-----------------------------------------------------------------------------

ipcDConnectService *ipcDConnectService::mInstance = nsnull;

//-----------------------------------------------------------------------------

static nsresult
SetupPeerInstance(PRUint32 aPeerID, DConnectSetup *aMsg, PRUint32 aMsgLen,
                  void **aInstancePtr);

//-----------------------------------------------------------------------------

// A wrapper class holding an instance to an in-process XPCOM object.

class DConnectInstance
{
public:
  DConnectInstance(PRUint32 peer, nsIInterfaceInfo *iinfo, nsISupports *instance)
    : mPeer(peer)
    , mIInfo(iinfo)
    , mInstance(instance)
  {}

  nsISupports      *RealInstance()  { return mInstance; }
  nsIInterfaceInfo *InterfaceInfo() { return mIInfo; }
  PRUint32          Peer()          { return mPeer; }

  DConnectInstanceKey::Key GetKey() {
    const nsID *iid;
    mIInfo->GetIIDShared(&iid);
    return DConnectInstanceKey::Key(mPeer, mInstance, iid);
  }

  NS_IMETHODIMP_(nsrefcnt) AddRef(void)
  {
    NS_PRECONDITION(PRInt32(mRefCnt) >= 0, "illegal refcnt");
    nsrefcnt count;
    count = PR_AtomicIncrement((PRInt32*)&mRefCnt);
    return count;
  }

  NS_IMETHODIMP_(nsrefcnt) Release(void)
  {
    nsrefcnt count;
    NS_PRECONDITION(0 != mRefCnt, "dup release");
    count = PR_AtomicDecrement((PRInt32 *)&mRefCnt);
    if (0 == count) {
//      mRefCnt = 1; /* stabilize */
      // ipcDConnectService is guaranteed to still exist here
      // (DConnectInstance lifetime is bound to ipcDConnectService)
      nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
      if (dConnect)
        dConnect->DeleteInstance(this);
      else
        NS_NOTREACHED("ipcDConnectService has gone before DConnectInstance");
      delete this;
      return 0;
    }
    return count;
  }

  // this gets called after calling AddRef() on an instance passed to the
  // client over IPC in order to have a count of IPC client-related references
  // separately from the overall reference count
  NS_IMETHODIMP_(nsrefcnt) AddRefIPC(void)
  {
    NS_PRECONDITION(PRInt32(mRefCntIPC) >= 0, "illegal refcnt");
    nsrefcnt count;
    count = PR_AtomicIncrement((PRInt32*)&mRefCntIPC);
    return count;
  }

  // this gets called before calling Release() when DCON_OP_RELEASE is
  // received from the IPC client
  NS_IMETHODIMP_(nsrefcnt) ReleaseIPC(void)
  {
    NS_PRECONDITION(0 != mRefCntIPC, "dup release");
    nsrefcnt count;
    count = PR_AtomicDecrement((PRInt32 *)&mRefCntIPC);
    return count;
  }
  
private:
  nsAutoRefCnt               mRefCnt;
  nsAutoRefCnt               mRefCntIPC;
  PRUint32                   mPeer;  // peer process "owning" this instance
  nsCOMPtr<nsIInterfaceInfo> mIInfo;
  nsCOMPtr<nsISupports>      mInstance;
};

void
ipcDConnectService::ReleaseWrappers(nsVoidArray &wrappers)
{
  for (PRInt32 i=0; i<wrappers.Count(); ++i)
  {
    ((DConnectInstance *) wrappers[i])->ReleaseIPC();
    ((DConnectInstance *) wrappers[i])->Release();
  }
}

//-----------------------------------------------------------------------------

static nsresult
SerializeParam(ipcMessageWriter &writer, const nsXPTType &t, const nsXPTCMiniVariant &v)
{
  switch (t.TagPart())
  {
    case nsXPTType::T_I8:
    case nsXPTType::T_U8:
      writer.PutInt8(v.val.u8);
      break;

    case nsXPTType::T_I16:
    case nsXPTType::T_U16:
      writer.PutInt16(v.val.u16);
      break;

    case nsXPTType::T_I32:
    case nsXPTType::T_U32:
      writer.PutInt32(v.val.u32);
      break;

    case nsXPTType::T_I64:
    case nsXPTType::T_U64:
      writer.PutBytes(&v.val.u64, sizeof(PRUint64));
      break;

    case nsXPTType::T_FLOAT:
      writer.PutBytes(&v.val.f, sizeof(float));
      break;

    case nsXPTType::T_DOUBLE:
      writer.PutBytes(&v.val.d, sizeof(double));
      break;

    case nsXPTType::T_BOOL:
      writer.PutBytes(&v.val.b, sizeof(PRBool));
      break;

    case nsXPTType::T_CHAR:
      writer.PutBytes(&v.val.c, sizeof(char));
      break;

    case nsXPTType::T_WCHAR:
      writer.PutBytes(&v.val.wc, sizeof(PRUnichar));
      break;

    case nsXPTType::T_IID:
      writer.PutBytes(v.val.p, sizeof(nsID));
      break;

    case nsXPTType::T_CHAR_STR:
      {
        if (v.val.p)
        {
          int len = strlen((const char *) v.val.p);
          writer.PutInt32(len);
          writer.PutBytes(v.val.p, len);
        }
        else
        {
          // put -1 to indicate null string
          writer.PutInt32((PRUint32) -1);
        }
      }
      break;

    case nsXPTType::T_WCHAR_STR:
      {
        if (v.val.p)
        {
          int len = 2 * nsCRT::strlen((const PRUnichar *) v.val.p);
          writer.PutInt32(len);
          writer.PutBytes(v.val.p, len);
        }
        else
        {
          // put -1 to indicate null string
          writer.PutInt32((PRUint32) -1);
        }
      }
      break;

    case nsXPTType::T_INTERFACE:
    case nsXPTType::T_INTERFACE_IS:
      NS_NOTREACHED("this should be handled elsewhere");
      return NS_ERROR_UNEXPECTED;

    case nsXPTType::T_ASTRING:
    case nsXPTType::T_DOMSTRING:
      {
        const nsAString *str = (const nsAString *) v.val.p;

        PRUint32 len = 2 * str->Length();
        nsAString::const_iterator begin;
        const PRUnichar *data = str->BeginReading(begin).get();

        writer.PutInt32(len);
        writer.PutBytes(data, len);
      }
      break;

    case nsXPTType::T_UTF8STRING:
    case nsXPTType::T_CSTRING:
      {
        const nsACString *str = (const nsACString *) v.val.p;

        PRUint32 len = str->Length();
        nsACString::const_iterator begin;
        const char *data = str->BeginReading(begin).get();

        writer.PutInt32(len);
        writer.PutBytes(data, len);
      }
      break;

    case nsXPTType::T_ARRAY:
      LOG(("array types are not yet supported\n"));
      return NS_ERROR_NOT_IMPLEMENTED;

    case nsXPTType::T_VOID:
    case nsXPTType::T_PSTRING_SIZE_IS:
    case nsXPTType::T_PWSTRING_SIZE_IS:
    default:
      LOG(("unexpected parameter type\n"));
      return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;
}

static nsresult
DeserializeParam(ipcMessageReader &reader, const nsXPTType &t, nsXPTCVariant &v)
{
  // defaults
  v.ptr = nsnull;
  v.type = t;
  v.flags = 0;

  switch (t.TagPart())
  {
    case nsXPTType::T_I8:
    case nsXPTType::T_U8:
      v.val.u8 = reader.GetInt8();
      break;

    case nsXPTType::T_I16:
    case nsXPTType::T_U16:
      v.val.u16 = reader.GetInt16();
      break;

    case nsXPTType::T_I32:
    case nsXPTType::T_U32:
      v.val.u32 = reader.GetInt32();
      break;

    case nsXPTType::T_I64:
    case nsXPTType::T_U64:
      reader.GetBytes(&v.val.u64, sizeof(v.val.u64));
      break;

    case nsXPTType::T_FLOAT:
      reader.GetBytes(&v.val.f, sizeof(v.val.f));
      break;

    case nsXPTType::T_DOUBLE:
      reader.GetBytes(&v.val.d, sizeof(v.val.d));
      break;

    case nsXPTType::T_BOOL:
      reader.GetBytes(&v.val.b, sizeof(v.val.b));
      break;

    case nsXPTType::T_CHAR:
      reader.GetBytes(&v.val.c, sizeof(v.val.c));
      break;

    case nsXPTType::T_WCHAR:
      reader.GetBytes(&v.val.wc, sizeof(v.val.wc));
      break;

    case nsXPTType::T_IID:
      {
        nsID *buf = (nsID *) malloc(sizeof(nsID));
        reader.GetBytes(buf, sizeof(nsID));
        v.val.p = v.ptr = buf;
        v.flags = nsXPTCVariant::PTR_IS_DATA | nsXPTCVariant::VAL_IS_ALLOCD;
      }
      break;

    case nsXPTType::T_CHAR_STR:
      {
        PRUint32 len = reader.GetInt32();
        if (len == (PRUint32) -1)
        {
          // it's a null string
          v.val.p = v.ptr = 0;
          v.flags = nsXPTCVariant::PTR_IS_DATA;
        }
        else
        {
          char *buf = (char *) malloc(len + 1);
          reader.GetBytes(buf, len);
          buf[len] = char(0);

          v.val.p = v.ptr = buf;
          v.flags = nsXPTCVariant::PTR_IS_DATA | nsXPTCVariant::VAL_IS_ALLOCD;
        }
      }
      break;

    case nsXPTType::T_WCHAR_STR:
      {
        PRUint32 len = reader.GetInt32();
        if (len == (PRUint32) -1)
        {
          // it's a null string
          v.val.p = v.ptr = 0;
          v.flags = nsXPTCVariant::PTR_IS_DATA;
        }
        else
        {
          PRUnichar *buf = (PRUnichar *) malloc(len + 2);
          reader.GetBytes(buf, len);
          buf[len / 2] = PRUnichar(0);

          v.val.p = v.ptr = buf;
          v.flags = nsXPTCVariant::PTR_IS_DATA | nsXPTCVariant::VAL_IS_ALLOCD;
        }
      }
      break;

    case nsXPTType::T_INTERFACE:
    case nsXPTType::T_INTERFACE_IS:
      {
        reader.GetBytes(&v.ptr, sizeof(void *));
        v.val.p = nsnull;
        v.flags = nsXPTCVariant::PTR_IS_DATA;
      }
      break;

    case nsXPTType::T_ASTRING:
    case nsXPTType::T_DOMSTRING:
      {
        PRUint32 len = reader.GetInt32();

        nsString *str = new nsString();
        str->SetLength(len / 2);
        PRUnichar *buf = str->BeginWriting();
        reader.GetBytes(buf, len);

        v.val.p = v.ptr = str;
        v.flags = nsXPTCVariant::PTR_IS_DATA | nsXPTCVariant::VAL_IS_DOMSTR;
      }
      break;

    case nsXPTType::T_UTF8STRING:
    case nsXPTType::T_CSTRING:
      {
        PRUint32 len = reader.GetInt32();

        nsCString *str = new nsCString();
        str->SetLength(len);
        char *buf = str->BeginWriting();
        reader.GetBytes(buf, len);

        v.val.p = v.ptr = str;
        v.flags = nsXPTCVariant::PTR_IS_DATA;

        // this distinction here is pretty pointless
        if (t.TagPart() == nsXPTType::T_CSTRING)
          v.flags |= nsXPTCVariant::VAL_IS_CSTR;
        else
          v.flags |= nsXPTCVariant::VAL_IS_UTF8STR;
      }
      break;

    case nsXPTType::T_ARRAY:
      LOG(("array types are not yet supported\n"));
      return NS_ERROR_NOT_IMPLEMENTED;

    case nsXPTType::T_VOID:
    case nsXPTType::T_PSTRING_SIZE_IS:
    case nsXPTType::T_PWSTRING_SIZE_IS:
    default:
      LOG(("unexpected parameter type\n"));
      return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;
}

static nsresult
SetupParam(const nsXPTParamInfo &p, nsXPTCVariant &v)
{
  const nsXPTType &t = p.GetType();

  if (p.IsIn() && p.IsDipper())
  {
    v.ptr = nsnull;

    switch (t.TagPart())
    {
      case nsXPTType::T_ASTRING:
      case nsXPTType::T_DOMSTRING:
        v.ptr = new nsString();
        if (!v.ptr)
          return NS_ERROR_OUT_OF_MEMORY;
        v.val.p = v.ptr;
        v.type = t;
        v.flags = nsXPTCVariant::PTR_IS_DATA | nsXPTCVariant::VAL_IS_DOMSTR;
        break;

      case nsXPTType::T_UTF8STRING:
      case nsXPTType::T_CSTRING:
        v.ptr = new nsCString();
        if (!v.ptr)
          return NS_ERROR_OUT_OF_MEMORY;
        v.val.p = v.ptr;
        v.type = t;
        v.flags = nsXPTCVariant::PTR_IS_DATA | nsXPTCVariant::VAL_IS_CSTR;
        break;

      default:
        LOG(("unhandled dipper: type=%d\n", t.TagPart()));
        return NS_ERROR_UNEXPECTED;
    }
  }
  else if (p.IsOut() || p.IsRetval())
  {
    memset(&v.val, 0, sizeof(v.val));
    v.ptr = &v.val;
    v.type = t;
    v.flags = nsXPTCVariant::PTR_IS_DATA;
  }

  return NS_OK;
}

static void
FinishParam(nsXPTCVariant &v)
{
  if (!v.val.p)
    return;

  if (v.IsValAllocated())
    free(v.val.p);
  else if (v.IsValInterface())
    ((nsISupports *) v.val.p)->Release();
  else if (v.IsValDOMString())
    delete (nsAString *) v.val.p;
  else if (v.IsValUTF8String() || v.IsValCString())
    delete (nsACString *) v.val.p;
}

static nsresult
DeserializeResult(ipcMessageReader &reader, const nsXPTType &t, nsXPTCMiniVariant &v)
{
  if (v.val.p == nsnull)
    return NS_OK;

  switch (t.TagPart())
  {
    case nsXPTType::T_I8:
    case nsXPTType::T_U8:
      *((PRUint8 *) v.val.p) = reader.GetInt8();
      break;

    case nsXPTType::T_I16:
    case nsXPTType::T_U16:
      *((PRUint16 *) v.val.p) = reader.GetInt16();
      break;

    case nsXPTType::T_I32:
    case nsXPTType::T_U32:
      *((PRUint32 *) v.val.p) = reader.GetInt32();
      break;

    case nsXPTType::T_I64:
    case nsXPTType::T_U64:
      reader.GetBytes(v.val.p, sizeof(PRUint64));
      break;

    case nsXPTType::T_FLOAT:
      reader.GetBytes(v.val.p, sizeof(float));
      break;

    case nsXPTType::T_DOUBLE:
      reader.GetBytes(v.val.p, sizeof(double));
      break;

    case nsXPTType::T_BOOL:
      reader.GetBytes(v.val.p, sizeof(PRBool));
      break;

    case nsXPTType::T_CHAR:
      reader.GetBytes(v.val.p, sizeof(char));
      break;

    case nsXPTType::T_WCHAR:
      reader.GetBytes(v.val.p, sizeof(PRUnichar));
      break;

    case nsXPTType::T_IID:
      {
        nsID *buf = (nsID *) nsMemory::Alloc(sizeof(nsID));
        reader.GetBytes(buf, sizeof(nsID));
        *((nsID **) v.val.p) = buf;
      }
      break;

    case nsXPTType::T_CHAR_STR:
      {
        PRUint32 len = reader.GetInt32();
        if (len == (PRUint32) -1)
        {
          // it's a null string
          v.val.p = 0;
        }
        else
        {
          char *buf = (char *) nsMemory::Alloc(len + 1);
          reader.GetBytes(buf, len);
          buf[len] = char(0);

          *((char **) v.val.p) = buf;
        }
      }
      break;

    case nsXPTType::T_WCHAR_STR:
      {
        PRUint32 len = reader.GetInt32();
        if (len == (PRUint32) -1)
        {
          // it's a null string
          v.val.p = 0;
        }
        else
        {
          PRUnichar *buf = (PRUnichar *) nsMemory::Alloc(len + 2);
          reader.GetBytes(buf, len);
          buf[len / 2] = PRUnichar(0);

          *((PRUnichar **) v.val.p) = buf;
        }
      }
      break;

    case nsXPTType::T_INTERFACE:
    case nsXPTType::T_INTERFACE_IS:
      {
        // stub creation will be handled outside this routine.  we only
        // deserialize the DConAddr into v.val.p temporarily.
        void *ptr;
        reader.GetBytes(&ptr, sizeof(void *));
        *((void **) v.val.p) = ptr;
      }
      break;

    case nsXPTType::T_ASTRING:
    case nsXPTType::T_DOMSTRING:
      {
        PRUint32 len = reader.GetInt32();

        nsAString *str = (nsAString *) v.val.p;

        nsAString::iterator begin;
        str->SetLength(len / 2);
        str->BeginWriting(begin);

        reader.GetBytes(begin.get(), len);
      }
      break;

    case nsXPTType::T_UTF8STRING:
    case nsXPTType::T_CSTRING:
      {
        PRUint32 len = reader.GetInt32();

        nsACString *str = (nsACString *) v.val.p;

        nsACString::iterator begin;
        str->SetLength(len);
        str->BeginWriting(begin);

        reader.GetBytes(begin.get(), len);
      }
      break;

    case nsXPTType::T_ARRAY:
      LOG(("array types are not yet supported\n"));
      return NS_ERROR_NOT_IMPLEMENTED;

    case nsXPTType::T_VOID:
    case nsXPTType::T_PSTRING_SIZE_IS:
    case nsXPTType::T_PWSTRING_SIZE_IS:
    default:
      LOG(("unexpected parameter type\n"));
      return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------

static PRUint32
NewRequestIndex()
{
  static PRUint32 sRequestIndex;
  return ++sRequestIndex;
}

//-----------------------------------------------------------------------------

class DConnectMsgSelector : public ipcIMessageObserver
{
public:
  DConnectMsgSelector(PRUint32 peer, PRUint8 opCodeMajor, PRUint32 requestIndex)
    : mPeer (peer)
    , mOpCodeMajor(opCodeMajor)
    , mRequestIndex(requestIndex)
  {}

  // stack based only
  NS_IMETHOD_(nsrefcnt) AddRef() { return 1; }
  NS_IMETHOD_(nsrefcnt) Release() { return 1; }

  NS_IMETHOD QueryInterface(const nsIID &aIID, void **aInstancePtr);

  NS_IMETHOD OnMessageAvailable(PRUint32 aSenderID, const nsID &aTarget,
                                const PRUint8 *aData, PRUint32 aDataLen)
  {
    // accept special "client dead" messages for a given peer
    // (empty target id, zero data and data length)
    if (aSenderID == mPeer && aTarget.Equals(nsID()) && !aData && !aDataLen)
        return NS_OK;
    const DConnectOp *op = (const DConnectOp *) aData;
    // accept only reply messages with the given peer/opcode/index
    // (to prevent eating replies the other thread might be waiting for)
    // as well as any non-reply messages (to serve external requests that
    // might arrive while we're waiting for the given reply).
    if (aDataLen >= sizeof(DConnectOp) &&
        ((op->opcode_major != DCON_OP_SETUP_REPLY &&
	        op->opcode_major != DCON_OP_INVOKE_REPLY) ||
         (aSenderID == mPeer &&
          op->opcode_major == mOpCodeMajor &&
          op->request_index == mRequestIndex)))
      return NS_OK;
    else
      return IPC_WAIT_NEXT_MESSAGE;
  }

  const PRUint32 mPeer;
  const PRUint8 mOpCodeMajor;
  const PRUint32 mRequestIndex;
};
NS_IMPL_QUERY_INTERFACE1(DConnectMsgSelector, ipcIMessageObserver)

class DConnectCompletion : public ipcIMessageObserver
{
public:
  DConnectCompletion(PRUint32 peer, PRUint8 opCodeMajor, PRUint32 requestIndex)
    : mSelector(peer, opCodeMajor, requestIndex)
  {}

  // stack based only
  NS_IMETHOD_(nsrefcnt) AddRef() { return 1; }
  NS_IMETHOD_(nsrefcnt) Release() { return 1; }

  NS_IMETHOD QueryInterface(const nsIID &aIID, void **aInstancePtr);

  NS_IMETHOD OnMessageAvailable(PRUint32 aSenderID, const nsID &aTarget,
                                const PRUint8 *aData, PRUint32 aDataLen)
  {
    const DConnectOp *op = (const DConnectOp *) aData;
    LOG((
      "DConnectCompletion::OnMessageAvailable: "
      "senderID=%d, opcode_major=%d, index=%d (waiting for %d)\n",
      aSenderID, op->opcode_major, op->request_index, mSelector.mRequestIndex
    ));
    if (aSenderID == mSelector.mPeer &&
        op->opcode_major == mSelector.mOpCodeMajor &&
        op->request_index == mSelector.mRequestIndex)
    {
      OnResponseAvailable(aSenderID, op, aDataLen);
    }
    else
    {
      // ensure ipcDConnectService is not deleted before we finish
      nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
      if (dConnect)
        dConnect->OnMessageAvailable(aSenderID, aTarget, aData, aDataLen);
    }
    return NS_OK;
  }

  virtual void OnResponseAvailable(PRUint32 sender, const DConnectOp *op, PRUint32 opLen) = 0;

  DConnectMsgSelector &GetSelector()
  {
     return mSelector;
  }

protected:
  DConnectMsgSelector mSelector;
};
NS_IMPL_QUERY_INTERFACE1(DConnectCompletion, ipcIMessageObserver)

//-----------------------------------------------------------------------------

class DConnectInvokeCompletion : public DConnectCompletion
{
public:
  DConnectInvokeCompletion(PRUint32 peer, const DConnectInvoke *invoke)
    : DConnectCompletion(peer, DCON_OP_INVOKE_REPLY, invoke->request_index)
    , mReply(nsnull)
    , mParamsLen(0)
  {}

  ~DConnectInvokeCompletion() { if (mReply) free(mReply); }

  void OnResponseAvailable(PRUint32 sender, const DConnectOp *op, PRUint32 opLen)
  {
    mReply = (DConnectInvokeReply *) malloc(opLen);
    memcpy(mReply, op, opLen);

    // the length in bytes of the parameter blob
    mParamsLen = opLen - sizeof(*mReply);
  }

  PRBool IsPending() const { return mReply == nsnull; }
  nsresult GetResult() const { return mReply->result; }

  const PRUint8 *Params() const { return (const PRUint8 *) (mReply + 1); }
  PRUint32 ParamsLen() const { return mParamsLen; }

  const DConnectInvokeReply *Reply() const { return mReply; }

private:
  DConnectInvokeReply *mReply;
  PRUint32             mParamsLen;
};

//-----------------------------------------------------------------------------

#define DCONNECT_STUB_ID                           \
{ /* 132c1f14-5442-49cb-8fe6-e60214bbf1db */       \
  0x132c1f14,                                      \
  0x5442,                                          \
  0x49cb,                                          \
  {0x8f, 0xe6, 0xe6, 0x02, 0x14, 0xbb, 0xf1, 0xdb} \
}
static NS_DEFINE_IID(kDConnectStubID, DCONNECT_STUB_ID);

// this class represents the non-local object instance.

class DConnectStub : public nsXPTCStubBase
{
public:
  NS_DECL_ISUPPORTS

  DConnectStub(nsIInterfaceInfo *aIInfo,
               DConAddr aInstance,
               PRUint32 aPeerID)
    : mIInfo(aIInfo)
    , mInstance(aInstance)
    , mPeerID(aPeerID)
    , mISupportsInstance(0)
    , mRefCntLevels(0)
    {}

  NS_HIDDEN ~DConnectStub();

  // return a refcounted pointer to the InterfaceInfo for this object
  // NOTE: on some platforms this MUST not fail or we crash!
  NS_IMETHOD GetInterfaceInfo(nsIInterfaceInfo **aInfo);

  // call this method and return result
  NS_IMETHOD CallMethod(PRUint16 aMethodIndex,
                        const nsXPTMethodInfo *aInfo,
                        nsXPTCMiniVariant *aParams);

  DConAddr Instance() { return mInstance; }
  PRUint32 PeerID()   { return mPeerID; }

  DConnectStubKey::Key GetKey() {
    return DConnectStubKey::Key(mPeerID, mInstance);
  }
  
  NS_IMETHOD_(nsrefcnt) AddRefIPC();
  
private:
  nsCOMPtr<nsIInterfaceInfo> mIInfo;

  // uniquely identifies this object instance between peers.
  DConAddr mInstance;

  // the "client id" of our IPC peer.  this guy owns the real object.
  PRUint32 mPeerID;
  
  // cached nsISupports instance for this object
  DConAddr mISupportsInstance;
  
  // stack of reference counter values returned by AddRef made in CreateStub
  // (access must be protected using the ipcDConnectService::stubLock())
  nsDeque mRefCntLevels;
};

NS_IMETHODIMP_(nsrefcnt)
DConnectStub::AddRefIPC()
{
  // in this special version, we memorize the resulting reference count in the
  // associated stack array. This stack is then used by Release() to determine
  // when it is necessary to send a RELEASE request to the peer owning the
  // object in order to balance AddRef() the peer does on DConnectInstance every
  // time it passes an object over IPC.
  
  nsAutoLock stubLock (ipcDConnectService::GetInstance()->StubLock()); 
  
  nsrefcnt count = AddRef();
  mRefCntLevels.Push((void *) count);
  return count;
}

nsresult
ipcDConnectService::CreateStub(const nsID &iid, PRUint32 peer, DConAddr instance,
                               DConnectStub **result)
{
  nsresult rv;

  nsCOMPtr<nsIInterfaceInfo> iinfo;
  rv = GetInterfaceInfo(iid, getter_AddRefs(iinfo));
  if (NS_FAILED(rv))
    return rv;

  nsAutoLock lock (mLock);

  if (mDisconnected)
      return NS_ERROR_NOT_INITIALIZED;

  DConnectStub *stub = nsnull;

  // first try to find an existing stub for a given peer and instance
  // (we do not care about IID because every DConAddr instance represents
  // exactly one interface of the real object on the peer's side)
  if (!mStubs.Get(DConnectStubKey::Key(peer, instance), &stub))
  {
    stub = new DConnectStub(iinfo, instance, peer);

    if (NS_UNLIKELY(!stub))
      rv = NS_ERROR_OUT_OF_MEMORY;
    else
    {
      rv = StoreStub(stub);
      if (NS_FAILED(rv))
        delete stub;
    }
  }

  if (NS_SUCCEEDED(rv))
  {
    stub->AddRefIPC();
    *result = stub;
  }

  return rv;
}

nsresult
ipcDConnectService::SerializeInterfaceParam(ipcMessageWriter &writer,
                                            PRUint32 peer, const nsID &iid,
                                            nsISupports *obj,
                                            nsVoidArray &wrappers)
{
  nsAutoLock lock (mLock);

  if (mDisconnected)
      return NS_ERROR_NOT_INITIALIZED;

  // we create an instance wrapper, and assume that the other side will send a
  // RELEASE message when it no longer needs the instance wrapper.  that will
  // usually happen after the call returns.
  //
  // XXX a lazy scheme might be better, but for now simplicity wins.

  // if the interface pointer references a DConnectStub corresponding
  // to an object in the address space of the peer, then no need to
  // create a new wrapper.

  // if the interface pointer references an object for which we already have
  // an existing wrapper, then we use it instead of creating a new one.  this
  // is based on the assumption that a valid COM object always returns exactly
  // the same pointer value in response to every
  // QueryInterface(NS_GET_IID(nsISupports), ...).

  if (!obj)
  {
    // write null address
    writer.PutBytes(&obj, sizeof(obj));
  }
  else
  {
    DConnectStub *stub = nsnull;
    nsresult rv = obj->QueryInterface(kDConnectStubID, (void **) &stub);
    if (NS_SUCCEEDED(rv) && (stub->PeerID() == peer))
    {
      void *p = stub->Instance();
      writer.PutBytes(&p, sizeof(p));
    }
    else
    {
      // create instance wrapper

      nsCOMPtr<nsIInterfaceInfo> iinfo;
      rv = GetInterfaceInfo(iid, getter_AddRefs(iinfo));
      if (NS_FAILED(rv))
        return rv;

      DConnectInstance *wrapper = nsnull;

      // first try to find an existing wrapper for the given object
      if (!FindInstanceAndAddRef(peer, obj, &iid, &wrapper))
      {
        wrapper = new DConnectInstance(peer, iinfo, obj);
        if (!wrapper)
          return NS_ERROR_OUT_OF_MEMORY;

        rv = StoreInstance(wrapper);
        if (NS_FAILED(rv))
        {
          delete wrapper;
          return rv;
        }

        // reference the newly created wrapper
        wrapper->AddRef();
      }
        
      if (!wrappers.AppendElement(wrapper))
      {
        wrapper->Release();
        return NS_ERROR_OUT_OF_MEMORY;
      }

      // wrapper remains referenced when passing it to the client
      // (will be released upon DCON_OP_RELEASE). increase the
      // second, IPC-only, reference counter
      wrapper->AddRefIPC();
      
      // send address of the instance wrapper, and set the low bit
      // to indicate that this is an instance wrapper.
      PtrBits bits = ((PtrBits) wrapper) | 0x1;
      writer.PutBytes(&bits, sizeof(bits));
    }
    NS_IF_RELEASE(stub);
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------

#define EXCEPTION_STUB_ID                          \
{ /* 70578d68-b25e-4370-a70c-89bbe56e6699 */       \
  0x70578d68,                                      \
  0xb25e,                                          \
  0x4370,                                          \
  {0xa7, 0x0c, 0x89, 0xbb, 0xe5, 0x6e, 0x66, 0x99} \
}
static NS_DEFINE_IID(kExceptionStubID, EXCEPTION_STUB_ID);

// ExceptionStub is used to cache all primitive-typed bits of a remote nsIException
// instance (such as the error message or line number) to:
//
// a) reduce the number of IPC calls;
// b) make sure exception information is available to the calling party even if
//    the called party terminates immediately after returning an exception.
//    To achieve this, all cacheable information is serialized together with
//    the instance wrapper itself.

class ExceptionStub : public nsIException
{
public:

  NS_DECL_ISUPPORTS
  NS_DECL_NSIEXCEPTION

  ExceptionStub(const nsACString &aMessage, nsresult aResult,
                const nsACString &aName, const nsACString &aFilename,
                PRUint32 aLineNumber, PRUint32 aColumnNumber,
                DConnectStub *aXcptStub)
    : mMessage(aMessage), mResult(aResult)
    , mName(aName), mFilename(aFilename)
    , mLineNumber (aLineNumber), mColumnNumber (aColumnNumber)
    , mXcptStub (aXcptStub) { NS_ASSERTION(aXcptStub, "NULL"); }

  ~ExceptionStub() {}

  nsIException *Exception() { return (nsIException *)(nsISupports *) mXcptStub; }
  DConnectStub *Stub() { return mXcptStub; }

private:

  nsCString mMessage;
  nsresult mResult;
  nsCString mName;
  nsCString mFilename;
  PRUint32 mLineNumber;
  PRUint32 mColumnNumber;
  nsRefPtr<DConnectStub> mXcptStub;
};

NS_IMPL_ADDREF(ExceptionStub)
NS_IMPL_RELEASE(ExceptionStub)

NS_IMETHODIMP
ExceptionStub::QueryInterface(const nsID &aIID, void **aInstancePtr)
{
  NS_ASSERTION(aInstancePtr,
               "QueryInterface requires a non-NULL destination!");

  // used to discover if this is an ExceptionStub instance.
  if (aIID.Equals(kExceptionStubID))
  {
    *aInstancePtr = this;
    NS_ADDREF_THIS();
    return NS_OK;
  }

  // regular NS_IMPL_QUERY_INTERFACE1 sequence

  nsISupports* foundInterface = 0;

  if (aIID.Equals(NS_GET_IID(nsIException)))
    foundInterface = NS_STATIC_CAST(nsIException*, this);
  else
  if (aIID.Equals(NS_GET_IID(nsISupports)))
    foundInterface = NS_STATIC_CAST(nsISupports*,
                                    NS_STATIC_CAST(nsIException *, this));
  else
  if (mXcptStub)
  {
    // ask the real nsIException object
    return mXcptStub->QueryInterface(aIID, aInstancePtr);
  }

  nsresult status;
  if (!foundInterface)
    status = NS_NOINTERFACE;
  else
  {
    NS_ADDREF(foundInterface);
    status = NS_OK;
  }
  *aInstancePtr = foundInterface;
  return status;
}

/* readonly attribute string message; */
NS_IMETHODIMP ExceptionStub::GetMessage(char **aMessage)
{
  if (!aMessage)
    return NS_ERROR_INVALID_POINTER;
  *aMessage = ToNewCString(mMessage);
  return NS_OK;
}

/* readonly attribute nsresult result; */
NS_IMETHODIMP ExceptionStub::GetResult(nsresult *aResult)
{
  if (!aResult)
    return NS_ERROR_INVALID_POINTER;
  *aResult = mResult;
  return NS_OK;
}

/* readonly attribute string name; */
NS_IMETHODIMP ExceptionStub::GetName(char **aName)
{
  if (!aName)
    return NS_ERROR_INVALID_POINTER;
  *aName = ToNewCString(mName);
  return NS_OK;
}

/* readonly attribute string filename; */
NS_IMETHODIMP ExceptionStub::GetFilename(char **aFilename)
{
  if (!aFilename)
    return NS_ERROR_INVALID_POINTER;
  *aFilename = ToNewCString(mFilename);
  return NS_OK;
}

/* readonly attribute PRUint32 lineNumber; */
NS_IMETHODIMP ExceptionStub::GetLineNumber(PRUint32 *aLineNumber)
{
  if (!aLineNumber)
    return NS_ERROR_INVALID_POINTER;
  *aLineNumber = mLineNumber;
  return NS_OK;
}

/* readonly attribute PRUint32 columnNumber; */
NS_IMETHODIMP ExceptionStub::GetColumnNumber(PRUint32 *aColumnNumber)
{
  if (!aColumnNumber)
    return NS_ERROR_INVALID_POINTER;
  *aColumnNumber = mColumnNumber;
  return NS_OK;
}

/* readonly attribute nsIStackFrame location; */
NS_IMETHODIMP ExceptionStub::GetLocation(nsIStackFrame **aLocation)
{
  if (Exception())
    return Exception()->GetLocation (aLocation);
  return NS_ERROR_UNEXPECTED;
}

/* readonly attribute nsIException inner; */
NS_IMETHODIMP ExceptionStub::GetInner(nsIException **aInner)
{
  if (Exception())
    return Exception()->GetInner (aInner);
  return NS_ERROR_UNEXPECTED;
}

/* readonly attribute nsISupports data; */
NS_IMETHODIMP ExceptionStub::GetData(nsISupports * *aData)
{
  if (Exception())
    return Exception()->GetData (aData);
  return NS_ERROR_UNEXPECTED;
}

/* string toString (); */
NS_IMETHODIMP ExceptionStub::ToString(char **_retval)
{
  if (Exception())
    return Exception()->ToString (_retval);
  return NS_ERROR_UNEXPECTED;
}

nsresult
ipcDConnectService::SerializeException(ipcMessageWriter &writer,
                                       PRUint32 peer, nsIException *xcpt,
                                       nsVoidArray &wrappers)
{
  PRBool cache_fields = PR_FALSE;

  // first, seralize the nsIException pointer.  The code is merely the same as
  // in SerializeInterfaceParam() except that when the exception to serialize
  // is an ExceptionStub instance and the real instance it stores as mXcpt
  // is a DConnectStub corresponding to an object in the address space of the
  // peer, we simply pass that object back instead of creating a new wrapper.

  {
    nsAutoLock lock (mLock);

    if (mDisconnected)
      return NS_ERROR_NOT_INITIALIZED;

    if (!xcpt)
    {
      // write null address
      writer.PutBytes(&xcpt, sizeof(xcpt));
    }
    else
    {
      ExceptionStub *stub = nsnull;
      nsresult rv = xcpt->QueryInterface(kExceptionStubID, (void **) &stub);
      if (NS_SUCCEEDED(rv) && (stub->Stub()->PeerID() == peer))
      {
        // send the wrapper instance back to the peer
        void *p = stub->Stub()->Instance();
        writer.PutBytes(&p, sizeof(p));
      }
      else
      {
        // create instance wrapper

        const nsID &iid = nsIException::GetIID();
        nsCOMPtr<nsIInterfaceInfo> iinfo;
        rv = GetInterfaceInfo(iid, getter_AddRefs(iinfo));
        if (NS_FAILED(rv))
          return rv;

        DConnectInstance *wrapper = nsnull;

        // first try to find an existing wrapper for the given object
        if (!FindInstanceAndAddRef(peer, xcpt, &iid, &wrapper))
        {
          wrapper = new DConnectInstance(peer, iinfo, xcpt);
          if (!wrapper)
            return NS_ERROR_OUT_OF_MEMORY;

          rv = StoreInstance(wrapper);
          if (NS_FAILED(rv))
          {
            delete wrapper;
            return rv;
          }

          // reference the newly created wrapper
          wrapper->AddRef();
        }
        
        if (!wrappers.AppendElement(wrapper))
        {
          wrapper->Release();
          return NS_ERROR_OUT_OF_MEMORY;
        }

        // wrapper remains referenced when passing it to the client
        // (will be released upon DCON_OP_RELEASE). increase the
        // second, IPC-only, reference counter
        wrapper->AddRefIPC();

        // send address of the instance wrapper, and set the low bit
        // to indicate that this is an instance wrapper.
        PtrBits bits = ((PtrBits) wrapper) | 0x1;
        writer.PutBytes(&bits, sizeof(bits));

        // we want to cache fields to minimize the number of IPC calls when
        // accessing exception data on the peer side
        cache_fields = PR_TRUE;
      }
      NS_IF_RELEASE(stub);
    }
  }

  if (!cache_fields)
    return NS_OK;

  nsresult rv;
  nsXPIDLCString str;
  PRUint32 num;

  // message
  rv = xcpt->GetMessage(getter_Copies(str));
  if (NS_SUCCEEDED (rv))
  {
    PRUint32 len = str.Length();
    nsACString::const_iterator begin;
    const char *data = str.BeginReading(begin).get();
    writer.PutInt32(len);
    writer.PutBytes(data, len);
  }
  else
    writer.PutInt32(0);

  // result
  nsresult res = 0;
  xcpt->GetResult(&res);
  writer.PutInt32(res);

  // name
  rv = xcpt->GetName(getter_Copies(str));
  if (NS_SUCCEEDED (rv))
  {
    PRUint32 len = str.Length();
    nsACString::const_iterator begin;
    const char *data = str.BeginReading(begin).get();
    writer.PutInt32(len);
    writer.PutBytes(data, len);
  }
  else
    writer.PutInt32(0);

  // filename
  rv = xcpt->GetFilename(getter_Copies(str));
  if (NS_SUCCEEDED (rv))
  {
    PRUint32 len = str.Length();
    nsACString::const_iterator begin;
    const char *data = str.BeginReading(begin).get();
    writer.PutInt32(len);
    writer.PutBytes(data, len);
  }
  else
    writer.PutInt32(0);

  // lineNumber
  num = 0;
  xcpt->GetLineNumber(&num);
  writer.PutInt32(num);

  // columnNumber
  num = 0;
  xcpt->GetColumnNumber(&num);
  writer.PutInt32(num);

  return writer.HasError() ? NS_ERROR_OUT_OF_MEMORY : NS_OK;
}

nsresult
ipcDConnectService::DeserializeException(const PRUint8 *data,
                                         PRUint32 dataLen,
                                         PRUint32 peer,
                                         nsIException **xcpt)
{
  NS_ASSERTION (xcpt, "NULL");
  if (!xcpt)
    return NS_ERROR_INVALID_POINTER;

  ipcMessageReader reader(data, dataLen);

  nsresult rv;
  PRUint32 len;

  void *instance = 0;
  reader.GetBytes(&instance, sizeof(void *));
  if (reader.HasError())
    return NS_ERROR_INVALID_ARG;

  PtrBits bits = (PtrBits) (instance);

  if (bits & 0x1)
  {
    // pointer is a peer-side exception instance wrapper,
    // read cahced exception data and create a stub for it.

    nsCAutoString message;
    len = reader.GetInt32();
    if (len)
    {
      message.SetLength(len);
      char *buf = message.BeginWriting();
      reader.GetBytes(buf, len);
    }

    nsresult result = reader.GetInt32();

    nsCAutoString name;
    len = reader.GetInt32();
    if (len)
    {
      name.SetLength(len);
      char *buf = name.BeginWriting();
      reader.GetBytes(buf, len);
    }

    nsCAutoString filename;
    len = reader.GetInt32();
    if (len)
    {
      filename.SetLength(len);
      char *buf = filename.BeginWriting();
      reader.GetBytes(buf, len);
    }

    PRUint32 lineNumber = reader.GetInt32();
    PRUint32 columnNumber = reader.GetInt32();

    if (reader.HasError())
      rv = NS_ERROR_INVALID_ARG;
    else
    {
      DConAddr addr = (DConAddr) (bits & ~0x1);
      nsRefPtr<DConnectStub> stub;
      rv = CreateStub(nsIException::GetIID(), peer, addr,
                      getter_AddRefs(stub));
      if (NS_SUCCEEDED(rv))
      {
        // create a special exception "stub" with cached error info
        ExceptionStub *xcptStub =
          new ExceptionStub (message, result,
                             name, filename,
                             lineNumber, columnNumber,
                             stub);
        if (xcptStub)
        {
          *xcpt = xcptStub;
          NS_ADDREF(xcptStub);
        }
        else
          rv = NS_ERROR_OUT_OF_MEMORY;
      }
    }
  }
  else if (bits)
  {
    // pointer is to our instance wrapper for nsIException we've sent before
    // (the remote method we've called had called us back and got an exception
    // from us that it decided to return as its own result). Replace it with
    // the real instance.
    DConnectInstance *wrapper = (DConnectInstance *) bits;
    if (CheckInstanceAndAddRef(wrapper))
    {
      *xcpt = (nsIException *) wrapper->RealInstance();
      NS_ADDREF(wrapper->RealInstance());
      wrapper->Release();
      rv = NS_OK;
    }
    else
    {
      NS_NOTREACHED("instance wrapper not found");
      rv = NS_ERROR_INVALID_ARG;
    }
  }
  else
  {
    // the peer explicitly passed us a NULL exception to indicate that the
    // exception on the current thread should be reset
    *xcpt = NULL;
    return NS_OK;
  }


  return rv;
}

//-----------------------------------------------------------------------------

DConnectStub::~DConnectStub()
{
#ifdef IPC_LOGGING
  const char *name;
  mIInfo->GetNameShared(&name);
  LOG(("{%p} DConnectStub::<dtor>(): peer=%d instance=%p {%s}\n",
       this, mPeerID, mInstance, name));
#endif    
    
  nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
  if (dConnect)
  {
#ifdef NS_DEBUG
    {
      nsAutoLock stubLock (dConnect->StubLock());
      NS_ASSERTION(mRefCntLevels.GetSize() == 0, "refcnt levels are still left");
    }
#endif    
    dConnect->DeleteStub(this);
  }
}

NS_IMETHODIMP_(nsrefcnt)
DConnectStub::AddRef()
{
  nsrefcnt count;
  count = PR_AtomicIncrement((PRInt32*)&mRefCnt);
  NS_LOG_ADDREF(this, count, "DConnectStub", sizeof(*this));
  return count;
}

NS_IMETHODIMP_(nsrefcnt)
DConnectStub::Release()
{
  nsrefcnt count = PR_AtomicDecrement((PRInt32 *)&mRefCnt);
  NS_LOG_RELEASE(this, count, "DConnectStub");

  nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
  if (dConnect)
  {
    nsAutoLock stubLock (dConnect->StubLock());
      
    // mRefCntLevels may already be empty here (due to the "stabilize" trick below)
    if (mRefCntLevels.GetSize() > 0)
    {
      nsrefcnt top = (nsrefcnt) (long) mRefCntLevels.Peek();
      NS_ASSERTION(top <= count + 1, "refcount is beyond the top level"); 

      if (top == count + 1)
      {
        // refcount dropped to a value stored in ipcDConnectService::CreateStub.
        // Send a RELEASE request to the peer (see also AddRefIPC).

        // remove the top refcount value
        mRefCntLevels.Pop();

        stubLock.unlock();
      
        nsresult rv;

        DConnectRelease msg;
        msg.opcode_major = DCON_OP_RELEASE;
        msg.opcode_minor = 0;
        msg.request_index = 0; // not used, set to some unused value
        msg.instance = mInstance;
    
        // fire off asynchronously... we don't expect any response to this message.
        rv = IPC_SendMessage(mPeerID, kDConnectTargetID,
                           (const PRUint8 *) &msg, sizeof(msg));
        if (NS_FAILED(rv))
          NS_WARNING("failed to send RELEASE event");
      }
    }
  }
  
  if (0 == count) {
//    mRefCnt = 1; /* stabilize */
    delete this;
    return 0;
  }

  return count;
}

NS_IMETHODIMP
DConnectStub::QueryInterface(const nsID &aIID, void **aInstancePtr)
{
  // used to discover if this is a DConnectStub instance.
  if (aIID.Equals(kDConnectStubID))
  {
    *aInstancePtr = this;
    NS_ADDREF_THIS();
    return NS_OK;
  }

  PRBool isISupports = aIID.Equals(NS_GET_IID(nsISupports));
  
  // see if we have a nsISupports stub for this object 
  if (isISupports)
  {
    // according to the COM Identity Rule, an object should always return the
    // same nsISupports pointer on every QueryInterface call. Based on this,
    // we cache the nsISupports instance for this stub's object in order to
    // reduce the number of IPC calls.

    if (mISupportsInstance != 0)
    {
      // check that the instance is still valid by searching for a stub for it
      nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
      if (dConnect)
      {
        DConnectStub *stub = nsnull;
        if (dConnect->FindStubAndAddRef(mPeerID, mISupportsInstance, &stub))
        {
            LOG(("using cached nsISupports stub for peer object\n"));
            *aInstancePtr = stub;
            return NS_OK;
        }
      }
      
      // the stub for the instance has already been deleted 
      mISupportsInstance = 0;
    }
  }
  
  // else, we need to query the peer object
  LOG(("calling QueryInterface on peer object\n"));

  DConnectSetupQueryInterface msg;
  msg.opcode_minor = DCON_OP_SETUP_QUERY_INTERFACE;
  msg.iid = aIID;
  msg.instance = mInstance;

  nsresult rv = SetupPeerInstance(mPeerID, &msg, sizeof(msg), aInstancePtr);
  
  if (isISupports && NS_SUCCEEDED(rv))
  {
    // cache the nsISupports instance (SetupPeerInstance returns DConnectStub)
    DConnectStub *stub = (DConnectStub *) *aInstancePtr;
    mISupportsInstance = stub->mInstance;
  }
  
  return rv;
}

NS_IMETHODIMP
DConnectStub::GetInterfaceInfo(nsIInterfaceInfo **aInfo)
{
  NS_ADDREF(*aInfo = mIInfo);
  return NS_OK;
}

NS_IMETHODIMP
DConnectStub::CallMethod(PRUint16 aMethodIndex,
                         const nsXPTMethodInfo *aInfo,
                         nsXPTCMiniVariant *aParams)
{
  LOG(("DConnectStub::CallMethod [methodIndex=%hu]\n", aMethodIndex));

  nsresult rv;

  // reset the exception early.  this is necessary because we may return a
  // failure from here without setting an exception (which might be expected
  // by the caller to detect the error origin: the interface we are stubbing
  // may indicate in some way that it always sets the exception info on
  // failure, therefore an "infoless" failure means the origin is RPC).
  // besides that, resetting the excetion before every IPC call is exactly the
  // same thing as Win32 RPC does, so doing this is useful for getting
  // similarity in behaviors.

  nsCOMPtr <nsIExceptionService> es;
  es = do_GetService (NS_EXCEPTIONSERVICE_CONTRACTID, &rv);
  if (NS_FAILED (rv))
    return rv;
  nsCOMPtr <nsIExceptionManager> em;
  rv = es->GetCurrentExceptionManager (getter_AddRefs(em));  
  if (NS_FAILED (rv))
    return rv;
  rv = em->SetCurrentException(NULL);
  if (NS_FAILED (rv))
    return rv;
    
  // ensure ipcDConnectService is not deleted before we finish
  nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
  if (!dConnect)
    return NS_ERROR_FAILURE;

  // dump arguments

  PRUint8 i, paramCount = aInfo->GetParamCount();

  LOG(("  instance=%p\n", mInstance));
  LOG(("  name=%s\n", aInfo->GetName()));
  LOG(("  param-count=%u\n", (PRUint32) paramCount));

  ipcMessageWriter writer(16 * paramCount);

  // INVOKE message header
  DConnectInvoke invoke;
  invoke.opcode_major = DCON_OP_INVOKE;
  invoke.opcode_minor = 0;
  invoke.request_index = NewRequestIndex();
  invoke.instance = mInstance;
  invoke.method_index = aMethodIndex;

  LOG(("  request-index=%d\n", (PRUint32) invoke.request_index));

  writer.PutBytes(&invoke, sizeof(invoke));

  // list of wrappers that get created during parameter serialization.  if we
  // are unable to send the INVOKE message, then we'll clean these up.
  nsVoidArray wrappers;

  for (i=0; i<paramCount; ++i)
  {
    const nsXPTParamInfo &paramInfo = aInfo->GetParam(i);

    if (paramInfo.IsIn() && !paramInfo.IsDipper())
    {
      const nsXPTType &type = paramInfo.GetType();

      if (type.IsInterfacePointer())
      {
        nsID iid;
        rv = dConnect->GetIIDForMethodParam(mIInfo, aInfo, paramInfo, type,
                                             aMethodIndex, i, aParams, PR_FALSE, iid);
        if (NS_SUCCEEDED(rv))
          rv = dConnect->SerializeInterfaceParam(writer, mPeerID, iid,
                                                 (nsISupports *) aParams[i].val.p,
                                                 wrappers);
      }
      else
        rv = SerializeParam(writer, type, aParams[i]);

      if (NS_FAILED(rv))
        return rv;
    }
    else if ((paramInfo.IsOut() || paramInfo.IsRetval()) && !aParams[i].val.p)
    {
      // report error early if NULL pointer is passed as an output parameter
      return NS_ERROR_NULL_POINTER;
    }
  }

  // temporarily disable the DConnect target observer to block normal processing
  // of pending messages through the event queue.
  IPC_DISABLE_MESSAGE_OBSERVER_FOR_SCOPE(kDConnectTargetID);

  rv = IPC_SendMessage(mPeerID, kDConnectTargetID,
                       writer.GetBuffer(),
                       writer.GetSize());
  LOG(("DConnectStub::CallMethod: IPC_SendMessage()=%08X\n", rv));
  if (NS_FAILED(rv))
  {
    // INVOKE message wasn't delivered; clean up wrappers
    dConnect->ReleaseWrappers(wrappers);
    return rv;
  }

  // now, we wait for the method call to complete.  during that time, it's
  // possible that we'll receive other method call requests.  we'll process
  // those while waiting for out method call to complete.  it's critical that
  // we do so since those other method calls might need to complete before
  // out method call can complete!

  DConnectInvokeCompletion completion(mPeerID, &invoke);

  do
  {
    rv = IPC_WaitMessage(IPC_SENDER_ANY, kDConnectTargetID,
                         &completion.GetSelector(), &completion,
                         DCON_WAIT_TIMEOUT);
    LOG(("DConnectStub::CallMethod: IPC_WaitMessage()=%08X\n", rv));
    if (NS_FAILED(rv))
    {
      // INVOKE message wasn't received; clean up wrappers
      dConnect->ReleaseWrappers(wrappers);
      return rv;
    }
  }
  while (completion.IsPending());

  rv = completion.GetResult();
  if (NS_FAILED(rv))
  {
    NS_ASSERTION(completion.ParamsLen() >= sizeof(void*),
                 "invalid nsIException serialization length");
    if (completion.ParamsLen() >= sizeof(void*))
    {
      LOG(("got nsIException instance (%p), will create a stub\n",
           *((void **) completion.Params())));

      nsIException *xcpt = nsnull;
      nsresult rv2; // preserve rv for returning to the caller
      rv2 = dConnect->DeserializeException (completion.Params(),
                                            completion.ParamsLen(),
                                            mPeerID, &xcpt);
      if (NS_SUCCEEDED(rv2))
      {
        rv2 = em->SetCurrentException(xcpt);
        NS_IF_RELEASE(xcpt);
      }
      NS_ASSERTION(NS_SUCCEEDED(rv2), "failed to deserialize/set exception");
    }
  }
  else if (completion.ParamsLen() > 0)
  {
    ipcMessageReader reader(completion.Params(), completion.ParamsLen());

    PRUint8 i;

    // handle out-params and retvals: DCON_OP_INVOKE_REPLY has the data
    for (i=0; i<paramCount; ++i)
    {
      const nsXPTParamInfo &paramInfo = aInfo->GetParam(i);

      if (paramInfo.IsOut() || paramInfo.IsRetval())
        DeserializeResult(reader, paramInfo.GetType(), aParams[i]);
    }

    // fixup any interface pointers using a second pass so we can properly
    // handle INTERFACE_IS referencing an IID that is an out param!
    for (i=0; i<paramCount && NS_SUCCEEDED(rv); ++i)
    {
      const nsXPTParamInfo &paramInfo = aInfo->GetParam(i);
      if (aParams[i].val.p && (paramInfo.IsOut() || paramInfo.IsRetval()))
      {
        const nsXPTType &type = paramInfo.GetType();
        if (type.IsInterfacePointer())
        {
          PtrBits bits = (PtrBits) *((void **) aParams[i].val.p);
          if (bits & 0x1)
          {
            *((void **) aParams[i].val.p) = (void *) (bits & ~0x1);

            nsID iid;
            rv = dConnect->GetIIDForMethodParam(mIInfo, aInfo, paramInfo, type,
                                                 aMethodIndex, i, aParams, PR_FALSE, iid);
            if (NS_SUCCEEDED(rv))
            {
              DConnectStub *stub;
              void **pptr = (void **) aParams[i].val.p;
              rv = dConnect->CreateStub(iid, mPeerID, (DConAddr) *pptr, &stub);
              if (NS_SUCCEEDED(rv))
                *((nsISupports **) aParams[i].val.p) = stub;
            }
          }
          else if (bits)
          {
            // pointer is to one of our instance wrappers. Replace it with the
            // real instance.
            DConnectInstance *wrapper = (DConnectInstance *) bits;
            if (dConnect->CheckInstanceAndAddRef(wrapper))
            {
              *((void **) aParams[i].val.p) = wrapper->RealInstance();
              NS_ADDREF(wrapper->RealInstance());
              wrapper->Release();
            }
            else
            {
              NS_NOTREACHED("instance wrapper not found");
              rv = NS_ERROR_INVALID_ARG;
            }
          }
          else
          {
            *((void **) aParams[i].val.p) = nsnull;
          }
        }
      }
    }
  }

  return rv;
}

//-----------------------------------------------------------------------------

class DConnectSetupCompletion : public DConnectCompletion
{
public:
  DConnectSetupCompletion(PRUint32 peer, const DConnectSetup *setup)
    : DConnectCompletion(peer, DCON_OP_SETUP_REPLY, setup->request_index)
    , mSetup(setup)
    , mStatus(NS_OK)
  {}

  void OnResponseAvailable(PRUint32 sender, const DConnectOp *op, PRUint32 opLen)
  {
    if (op->opcode_major != DCON_OP_SETUP_REPLY)
    {
      NS_NOTREACHED("unexpected response");
      mStatus = NS_ERROR_UNEXPECTED;
      return;
    }

    if (opLen < sizeof(DConnectSetupReply))
    {
      NS_NOTREACHED("unexpected response size");
      mStatus = NS_ERROR_UNEXPECTED;
      return;
    }

    const DConnectSetupReply *reply = (const DConnectSetupReply *) op;

    LOG(("got SETUP_REPLY: status=%x instance=%p\n", reply->status, reply->instance));

    if (NS_FAILED(reply->status))
    {
      mStatus = reply->status;

      const PRUint8 *params = ((const PRUint8 *) op) + sizeof (DConnectSetupReply);
      const PRUint32 paramsLen = opLen - sizeof (DConnectSetupReply);

      NS_ASSERTION(paramsLen >= sizeof(void*),
                   "invalid nsIException serialization length");
      if (paramsLen >= sizeof(void*))
      {
        LOG(("got nsIException instance (%p), will create a stub\n",
             *((void **) params)));

        nsresult rv;
        nsCOMPtr <nsIExceptionService> es;
        es = do_GetService (NS_EXCEPTIONSERVICE_CONTRACTID, &rv);
        if (NS_SUCCEEDED(rv))
        {
          nsCOMPtr <nsIExceptionManager> em;
          rv = es->GetCurrentExceptionManager (getter_AddRefs(em));
          if (NS_SUCCEEDED(rv))
          {
            // ensure ipcDConnectService is not deleted before we finish
            nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
            if (dConnect)
            {
              nsIException *xcpt = nsnull;
              rv = dConnect->DeserializeException (params, paramsLen,
                                                   sender, &xcpt);
              if (NS_SUCCEEDED(rv))
              {
                rv = em->SetCurrentException(xcpt);
                NS_IF_RELEASE(xcpt);
              }
            }
            else
              rv = NS_ERROR_UNEXPECTED;
          }
        }
        NS_ASSERTION(NS_SUCCEEDED(rv), "failed to deserialize/set exception");
      }
    }
    else
    {
      // ensure ipcDConnectService is not deleted before we finish
      nsRefPtr <ipcDConnectService> dConnect (ipcDConnectService::GetInstance());
      nsresult rv;
      if (dConnect)
        rv = dConnect->CreateStub(mSetup->iid, sender, reply->instance,
                                  getter_AddRefs(mStub));
      else
        rv = NS_ERROR_FAILURE;
      if (NS_FAILED(rv))
        mStatus = rv;
    }
  }

  nsresult GetStub(void **aInstancePtr)
  {
    if (NS_FAILED(mStatus))
      return mStatus;

    DConnectStub *stub = mStub;
    NS_IF_ADDREF(stub);
    *aInstancePtr = stub;
    return NS_OK;
  }

private:
  const DConnectSetup    *mSetup;
  nsresult                mStatus;
  nsRefPtr<DConnectStub>  mStub;
};

// static
nsresult
SetupPeerInstance(PRUint32 aPeerID, DConnectSetup *aMsg, PRUint32 aMsgLen,
                  void **aInstancePtr)
{
  *aInstancePtr = nsnull;

  aMsg->opcode_major = DCON_OP_SETUP;
  aMsg->request_index = NewRequestIndex();

  // temporarily disable the DConnect target observer to block normal processing
  // of pending messages through the event queue.
  IPC_DISABLE_MESSAGE_OBSERVER_FOR_SCOPE(kDConnectTargetID);

  // send SETUP message, expect SETUP_REPLY

  nsresult rv = IPC_SendMessage(aPeerID, kDConnectTargetID,
                                (const PRUint8 *) aMsg, aMsgLen);
  if (NS_FAILED(rv))
    return rv;

  DConnectSetupCompletion completion(aPeerID, aMsg);

  // need to allow messages from other clients to be processed immediately
  // to avoid distributed dead locks.  the completion's OnMessageAvailable
  // will call our default OnMessageAvailable if it receives any message
  // other than the one for which it is waiting.

  do
  {
    rv = IPC_WaitMessage(IPC_SENDER_ANY, kDConnectTargetID,
			 &completion.GetSelector(), &completion,
                         DCON_WAIT_TIMEOUT);
    if (NS_FAILED(rv))
      break;

    rv = completion.GetStub(aInstancePtr);
  }
  while (NS_SUCCEEDED(rv) && *aInstancePtr == nsnull);

  return rv;
}

//-----------------------------------------------------------------------------

#if defined(DCONNECT_MULTITHREADED)

class DConnectWorker : public nsIRunnable
{
public:
  // no reference counting
  NS_IMETHOD_(nsrefcnt) AddRef() { return 1; }
  NS_IMETHOD_(nsrefcnt) Release() { return 1; }
  NS_IMETHOD QueryInterface(const nsIID &aIID, void **aInstancePtr);

  NS_DECL_NSIRUNNABLE

  DConnectWorker(ipcDConnectService *aDConnect) : mDConnect (aDConnect) {}
  NS_HIDDEN_(nsresult) Init();
  NS_HIDDEN_(void) Join() { mThread->Join(); };

private:
  nsCOMPtr <nsIThread> mThread;
  ipcDConnectService *mDConnect;
};

NS_IMPL_QUERY_INTERFACE1(DConnectWorker, nsIRunnable)

nsresult
DConnectWorker::Init()
{
  return NS_NewThread(getter_AddRefs(mThread), this, 0, PR_JOINABLE_THREAD);
}

NS_IMETHODIMP
DConnectWorker::Run()
{
  LOG(("DConnect Worker thread started.\n"));

  nsAutoMonitor mon(mDConnect->mPendingMon);

  while (!mDConnect->mDisconnected)
  {
    DConnectRequest *request = mDConnect->mPendingQ.First();
    if (!request)
    {
      mDConnect->mWaitingWorkers++;
      {
        // Note: we attempt to enter mWaitingWorkersMon from under mPendingMon
        // here, but it should be safe because it's the only place where it
        // happens. We could exit mPendingMon first, but we need to wait on it
        // shorltly afterwards, which in turn will require us to enter it again
        // just to exit immediately and start waiting. This seems to me a bit
        // stupid (exit->enter->exit->wait).
        nsAutoMonitor workersMon(mDConnect->mWaitingWorkersMon);
        workersMon.NotifyAll();
      }

      nsresult rv = mon.Wait();
      mDConnect->mWaitingWorkers--;

      if (NS_FAILED(rv))
        break;
    }
    else
    {
      LOG(("DConnect Worker thread got request.\n"));

      // remove the request from the queue
      mDConnect->mPendingQ.RemoveFirst();

      PRBool pendingQEmpty = mDConnect->mPendingQ.IsEmpty();
      mon.Exit();

      if (pendingQEmpty)
      {
        nsAutoMonitor workersMon(mDConnect->mWaitingWorkersMon);
        workersMon.NotifyAll();
      }

      // request is processed outside the queue monitor
      mDConnect->OnIncomingRequest(request->peer, request->op, request->opLen);
      delete request;

      mon.Enter();
    }
  }

  LOG(("DConnect Worker thread stopped.\n"));
  return NS_OK;
}

// called only on DConnect message thread
nsresult
ipcDConnectService::CreateWorker()
{
  DConnectWorker *worker = new DConnectWorker(this);
  if (!worker)
    return NS_ERROR_OUT_OF_MEMORY;
  nsresult rv = worker->Init();
  if (NS_SUCCEEDED(rv))
  {
    nsAutoLock lock(mLock);
    if (!mWorkers.AppendElement(worker))
      rv = NS_ERROR_OUT_OF_MEMORY;
  }
  if (NS_FAILED(rv))
    delete worker;
  return rv;
}

#endif // defined(DCONNECT_MULTITHREADED)

//-----------------------------------------------------------------------------

ipcDConnectService::ipcDConnectService()
 : mLock(NULL)
 , mDisconnected(PR_TRUE)
 , mStubLock(NULL)
{
}

PR_STATIC_CALLBACK(PLDHashOperator)
EnumerateInstanceMapAndDelete (const DConnectInstanceKey::Key &aKey,
                               DConnectInstance *aData,
                               void *userArg)
{
  // this method is to be called on ipcDConnectService shutdown only
  // (after which no DConnectInstances may exist), so forcibly delete them
  // disregarding the reference counter
    
#ifdef IPC_LOGGING
  const char *name;
  aData->InterfaceInfo()->GetNameShared(&name);
  LOG(("ipcDConnectService: WARNING: deleting unreleased "
       "instance=%p iface=%p {%s}\n", aData, aData->RealInstance(), name));
#endif

  delete aData;
  return PL_DHASH_NEXT;
}

ipcDConnectService::~ipcDConnectService()
{
  if (!mDisconnected)
    Shutdown();

  mInstance = nsnull;
  PR_DestroyLock(mStubLock);
  PR_DestroyLock(mLock);
}

//-----------------------------------------------------------------------------

nsresult
ipcDConnectService::Init()
{
  nsresult rv;

  rv = IPC_DefineTarget(kDConnectTargetID, this);
  if (NS_FAILED(rv))
    return rv;

  rv = IPC_AddClientObserver(this);
  if (NS_FAILED(rv))
    return rv;

  mLock = PR_NewLock();
  if (!mLock)
    return NS_ERROR_OUT_OF_MEMORY;

  if (!mInstances.Init())
    return NS_ERROR_OUT_OF_MEMORY;
  if (!mInstanceSet.Init())
    return NS_ERROR_OUT_OF_MEMORY;

  if (!mStubs.Init())
    return NS_ERROR_OUT_OF_MEMORY;

  mIIM = do_GetService(NS_INTERFACEINFOMANAGER_SERVICE_CONTRACTID, &rv);
  if (NS_FAILED(rv))
    return rv;

  mStubLock = PR_NewLock();
  if (!mStubLock)
    return NS_ERROR_OUT_OF_MEMORY;

#if defined(DCONNECT_MULTITHREADED)

  mPendingMon = nsAutoMonitor::NewMonitor("DConnect pendingQ monitor");
  if (!mPendingMon)
    return NS_ERROR_OUT_OF_MEMORY;

  mWaitingWorkers = 0;

  mWaitingWorkersMon = nsAutoMonitor::NewMonitor("DConnect waiting workers monitor");
  if (!mWaitingWorkersMon)
    return NS_ERROR_OUT_OF_MEMORY;

  // create a single worker thread
  rv = CreateWorker();
  if (NS_FAILED(rv))
      return rv;

#endif

  mDisconnected = PR_FALSE;
  mInstance = this;

  return NS_OK;
}

void
ipcDConnectService::Shutdown()
{
  {
    // set the disconnected flag to make sensitive public methods
    // unavailale from other (non worker) threads.
    nsAutoLock lock(mLock);
    mDisconnected = PR_TRUE;
  }

#if defined(DCONNECT_MULTITHREADED)

  {
    // remove all pending messages and wake up all workers.
    // mDisconnected is true here and they will terminate execution after
    // processing the last request.
    nsAutoMonitor mon(mPendingMon);
    mPendingQ.DeleteAll();
    mon.NotifyAll();
  }

#if defined(DCONNECT_STATS)
  printf("ipcDConnectService Stats\n");
  printf(" => number of worker threads: %d\n", mWorkers.Count());
#endif

  // destroy all worker threads
  for (int i = 0; i < mWorkers.Count(); i++)
  {
    DConnectWorker *worker = NS_STATIC_CAST(DConnectWorker *, mWorkers[i]);
    worker->Join();
    delete worker;
  }
  mWorkers.Clear();

  nsAutoMonitor::DestroyMonitor(mWaitingWorkersMon);
  nsAutoMonitor::DestroyMonitor(mPendingMon);

#endif

  // make sure we have released all instances
  mInstances.EnumerateRead(EnumerateInstanceMapAndDelete, nsnull);

  mInstanceSet.Clear();
  mInstances.Clear();

  // clear the stub table
  // (this will not release stubs -- it's the client's responsibility)
  mStubs.Clear();
}

// this should be inlined
nsresult
ipcDConnectService::GetInterfaceInfo(const nsID &iid, nsIInterfaceInfo **result)
{
  return mIIM->GetInfoForIID(&iid, result);
}

// this is adapted from the version in xpcwrappednative.cpp
nsresult
ipcDConnectService::GetIIDForMethodParam(nsIInterfaceInfo *iinfo,
                                         const nsXPTMethodInfo *methodInfo,
                                         const nsXPTParamInfo &paramInfo,
                                         const nsXPTType &type,
                                         PRUint16 methodIndex,
                                         PRUint8 paramIndex,
                                         nsXPTCMiniVariant *dispatchParams,
                                         PRBool isFullVariantArray,
                                         nsID &result)
{
  PRUint8 argnum, tag = type.TagPart();
  nsresult rv;

  if (tag == nsXPTType::T_INTERFACE)
  {
    rv = iinfo->GetIIDForParamNoAlloc(methodIndex, &paramInfo, &result);
  }
  else if (tag == nsXPTType::T_INTERFACE_IS)
  {
    rv = iinfo->GetInterfaceIsArgNumberForParam(methodIndex, &paramInfo, &argnum);
    if (NS_FAILED(rv))
      return rv;

    const nsXPTParamInfo& arg_param = methodInfo->GetParam(argnum);
    const nsXPTType& arg_type = arg_param.GetType();

    // The xpidl compiler ensures this. We reaffirm it for safety.
    if (!arg_type.IsPointer() || arg_type.TagPart() != nsXPTType::T_IID)
      return NS_ERROR_UNEXPECTED;

    nsID *p;
    if (isFullVariantArray)
      p = (nsID *) ((nsXPTCVariant *) dispatchParams)[argnum].val.p;
    else
      p = (nsID *) dispatchParams[argnum].val.p;
    if (!p)
      return NS_ERROR_UNEXPECTED;

    result = *p;
  }
  else
    rv = NS_ERROR_UNEXPECTED;
  return rv;
}

nsresult
ipcDConnectService::StoreInstance(DConnectInstance *wrapper)
{
#ifdef IPC_LOGGING
  const char *name;
  wrapper->InterfaceInfo()->GetNameShared(&name);
  LOG(("ipcDConnectService::StoreInstance(): instance=%p iface=%p {%s}\n",
       wrapper, wrapper->RealInstance(), name));
#endif

  nsresult rv = mInstanceSet.Put(wrapper);
  if (NS_SUCCEEDED(rv))
  {
    rv = mInstances.Put(wrapper->GetKey(), wrapper)
      ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
    if (NS_FAILED(rv))
      mInstanceSet.Remove(wrapper);
  }
  return rv;
}

void
ipcDConnectService::DeleteInstance(DConnectInstance *wrapper,
                                   PRBool locked /* = PR_FALSE */)
{
  if (!locked)
    PR_Lock(mLock);

#ifdef IPC_LOGGING
  const char *name;
  wrapper->InterfaceInfo()->GetNameShared(&name);
  LOG(("ipcDConnectService::DeleteInstance(): instance=%p iface=%p {%s}\n",
       wrapper, wrapper->RealInstance(), name));
#endif

  mInstances.Remove(wrapper->GetKey());
  mInstanceSet.Remove(wrapper);

  if (!locked)
    PR_Unlock(mLock);
}

PRBool
ipcDConnectService::FindInstanceAndAddRef(PRUint32 peer,
                                          const nsISupports *obj,
                                          const nsIID *iid,
                                          DConnectInstance **wrapper)
{
  PRBool result = mInstances.Get(DConnectInstanceKey::Key(peer, obj, iid), wrapper);
  if (result)
    (*wrapper)->AddRef();
  return result;
}

PRBool
ipcDConnectService::CheckInstanceAndAddRef(DConnectInstance *wrapper)
{
  nsAutoLock lock (mLock);

  PRBool result = mInstanceSet.Contains(wrapper);
  if (result)
    wrapper->AddRef();
  return result;
}

nsresult
ipcDConnectService::StoreStub(DConnectStub *stub)
{
#ifdef IPC_LOGGING
  const char *name;
  nsCOMPtr<nsIInterfaceInfo> iinfo;
  stub->GetInterfaceInfo(getter_AddRefs(iinfo));
  iinfo->GetNameShared(&name);
  LOG(("ipcDConnectService::StoreStub(): stub=%p instance=%p {%s}\n",
       stub, stub->Instance(), name));
#endif

  return mStubs.Put(stub->GetKey(), stub)
      ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

void
ipcDConnectService::DeleteStub(DConnectStub *stub)
{
#ifdef IPC_LOGGING
  const char *name;
  nsCOMPtr<nsIInterfaceInfo> iinfo;
  stub->GetInterfaceInfo(getter_AddRefs(iinfo));
  iinfo->GetNameShared(&name);
  LOG(("ipcDConnectService::DeleteStub(): stub=%p instance=%p {%s}\n",
       stub, stub->Instance(), name));
#endif

  nsAutoLock lock (mLock);

  // this method is intended to be called only from DConnectStub destructor.
  // the stub object is not deleted when removed from the table, because
  // DConnectStub pointers are not owned by mStubs.
  mStubs.Remove(stub->GetKey());
}

PRBool
ipcDConnectService::FindStubAndAddRef(PRUint32 peer, const DConAddr instance,
                                      DConnectStub **stub)
{
  nsAutoLock lock (mLock);

  PRBool result = mStubs.Get(DConnectStubKey::Key(peer, instance), stub);
  if (result)
    NS_ADDREF(*stub);
  return result;
}

NS_IMPL_THREADSAFE_ISUPPORTS3(ipcDConnectService, ipcIDConnectService,
                                                  ipcIMessageObserver,
                                                  ipcIClientObserver)

NS_IMETHODIMP
ipcDConnectService::CreateInstance(PRUint32 aPeerID,
                                   const nsID &aCID,
                                   const nsID &aIID,
                                   void **aInstancePtr)
{
  DConnectSetupClassID msg;
  msg.opcode_minor = DCON_OP_SETUP_NEW_INST_CLASSID;
  msg.iid = aIID;
  msg.classid = aCID;

  return SetupPeerInstance(aPeerID, &msg, sizeof(msg), aInstancePtr);
}

NS_IMETHODIMP
ipcDConnectService::CreateInstanceByContractID(PRUint32 aPeerID,
                                               const char *aContractID,
                                               const nsID &aIID,
                                               void **aInstancePtr)
{
  size_t slen = strlen(aContractID);
  size_t size = sizeof(DConnectSetupContractID) + slen;

  DConnectSetupContractID *msg =
      (DConnectSetupContractID *) malloc(size);

  msg->opcode_minor = DCON_OP_SETUP_NEW_INST_CONTRACTID;
  msg->iid = aIID;
  memcpy(&msg->contractid, aContractID, slen + 1);

  nsresult rv = SetupPeerInstance(aPeerID, msg, size, aInstancePtr);

  free(msg);
  return rv;
}

NS_IMETHODIMP
ipcDConnectService::GetService(PRUint32 aPeerID,
                               const nsID &aCID,
                               const nsID &aIID,
                               void **aInstancePtr)
{
  DConnectSetupClassID msg;
  msg.opcode_minor = DCON_OP_SETUP_GET_SERV_CLASSID;
  msg.iid = aIID;
  msg.classid = aCID;

  return SetupPeerInstance(aPeerID, &msg, sizeof(msg), aInstancePtr);
}

NS_IMETHODIMP
ipcDConnectService::GetServiceByContractID(PRUint32 aPeerID,
                                           const char *aContractID,
                                           const nsID &aIID,
                                           void **aInstancePtr)
{
  size_t slen = strlen(aContractID);
  size_t size = sizeof(DConnectSetupContractID) + slen;

  DConnectSetupContractID *msg =
      (DConnectSetupContractID *) malloc(size);

  msg->opcode_minor = DCON_OP_SETUP_GET_SERV_CONTRACTID;
  msg->iid = aIID;
  memcpy(&msg->contractid, aContractID, slen + 1);

  nsresult rv = SetupPeerInstance(aPeerID, msg, size, aInstancePtr);

  free(msg);
  return rv;
}

//-----------------------------------------------------------------------------

NS_IMETHODIMP
ipcDConnectService::OnMessageAvailable(PRUint32 aSenderID,
                                       const nsID &aTarget,
                                       const PRUint8 *aData,
                                       PRUint32 aDataLen)
{
  if (mDisconnected)
    return NS_ERROR_NOT_INITIALIZED;

  const DConnectOp *op = (const DConnectOp *) aData;

  LOG (("ipcDConnectService::OnMessageAvailable: "
        "senderID=%d, opcode_major=%d, index=%d\n",
        aSenderID, op->opcode_major, op->request_index));

#if defined(DCONNECT_MULTITHREADED)

  nsAutoMonitor mon(mPendingMon);
  mPendingQ.Append(new DConnectRequest(aSenderID, op, aDataLen));
  // notify all workers
  mon.NotifyAll();
  mon.Exit();

  // Yield the cpu so a worker can get a chance to start working without too much fuss.
  PR_Sleep(PR_INTERVAL_NO_WAIT);
  mon.Enter();
  // examine the queue
  if (!mPendingQ.IsEmpty() && !mWaitingWorkers)
  {
    // wait a little while to let the workers empty the queue.
    mon.Exit();
    {
      PRUint32 ticks = PR_MillisecondsToInterval(PR_MIN(mWorkers.Count() / 20 + 1, 10));
      nsAutoMonitor workersMon(mWaitingWorkersMon);
      workersMon.Wait(ticks);
    }
    mon.Enter();
    // examine the queue again
    if (!mPendingQ.IsEmpty() && !mWaitingWorkers)
    {
      // we need one more worker
      nsresult rv = CreateWorker();
      NS_ASSERTION(NS_SUCCEEDED(rv), "failed to create one more worker thread");
      rv = rv;
    }
  }

#else

  OnIncomingRequest(aSenderID, op, aDataLen);

#endif

  return NS_OK;
}

struct PruneInstanceMapForPeerArgs
{
  ipcDConnectService *that;
  PRUint32 clientID;
  nsVoidArray &wrappers;
};

PR_STATIC_CALLBACK(PLDHashOperator)
PruneInstanceMapForPeer (const DConnectInstanceKey::Key &aKey,
                         DConnectInstance *aData,
                         void *userArg)
{
  PruneInstanceMapForPeerArgs *args = (PruneInstanceMapForPeerArgs *)userArg;
  NS_ASSERTION(args, "PruneInstanceMapForPeerArgs is NULL");

  if (args && args->clientID == aData->Peer())
  {
    // add a fake reference to hold the wrapper alive
    nsrefcnt count = aData->AddRef();

    // release all IPC references for this wrapper, the client is now
    // officially dead (and thus cannot call AddRefIPC in the middle)
    nsrefcnt countIPC = aData->AddRefIPC();
    countIPC = aData->ReleaseIPC();

    LOG(("ipcDConnectService::PruneInstanceMapForPeer: "
         "instance=%p: %d IPC refs to release (total refcnt=%d)\n",
         aData, countIPC, count));

    while (countIPC)
    {
      countIPC = aData->ReleaseIPC();
      aData->Release();
    }

    // collect the instance for future destruction
    if (!args->wrappers.AppendElement(aData))
    {
      NS_NOTREACHED("Not enough memory");
      // bad but what to do
      delete aData;
    }
  }
  return PL_DHASH_NEXT;
}

NS_IMETHODIMP
ipcDConnectService::OnClientStateChange(PRUint32 aClientID,
                                        PRUint32 aClientState)
{
  LOG(("ipcDConnectService::OnClientStateChange: aClientID=%d, aClientState=%d\n",
       aClientID, aClientState));

  if (aClientState == ipcIClientObserver::CLIENT_DOWN)
  {
    if (aClientID == IPC_SENDER_ANY)
    {
      // a special case: our IPC system is being shutdown, try to safely
      // uninitialize everything...
      Shutdown();
    }
    else
    {
      LOG(("ipcDConnectService::OnClientStateChange: "
           "pruning all instances created for peer %d...\n", aClientID));

      nsVoidArray wrappers;

      {
        nsAutoLock lock (mLock);

        // make sure we have removed all instances from instance maps
        PruneInstanceMapForPeerArgs args = { this, aClientID, wrappers };
        mInstances.EnumerateRead(PruneInstanceMapForPeer, (void *)&args);
      }

      LOG(("ipcDConnectService::OnClientStateChange: "
           "%d lost instances (should be 0 unless the peer has "
           "crashed)\n", wrappers.Count()));

      // release all fake references we've added in PruneInstanceMapForPeer().
      // this may call wrapper destructors so it's important to do that
      // outside the lock because destructors will release the real
      // objects which may need to make asynchronous use our service
      for (PRInt32 i = 0; i < wrappers.Count(); ++i)
        ((DConnectInstance *) wrappers[i])->Release();
    }
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------

void
ipcDConnectService::OnIncomingRequest(PRUint32 peer, const DConnectOp *op, PRUint32 opLen)
{
  switch (op->opcode_major)
  {
    case DCON_OP_SETUP:
      OnSetup(peer, (const DConnectSetup *) op, opLen);
      break;
    case DCON_OP_RELEASE:
      OnRelease(peer, (const DConnectRelease *) op);
      break;
    case DCON_OP_INVOKE:
      OnInvoke(peer, (const DConnectInvoke *) op, opLen);
      break;
    default:
      NS_NOTREACHED("unknown opcode major");
  }
}

void
ipcDConnectService::OnSetup(PRUint32 peer, const DConnectSetup *setup, PRUint32 opLen)
{
  nsISupports *instance = nsnull;
  nsresult rv = NS_ERROR_FAILURE;

  switch (setup->opcode_minor)
  {
    // CreateInstance
    case DCON_OP_SETUP_NEW_INST_CLASSID:
    {
      const DConnectSetupClassID *setupCI = (const DConnectSetupClassID *) setup;

      nsCOMPtr<nsIComponentManager> compMgr;
      rv = NS_GetComponentManager(getter_AddRefs(compMgr));
      if (NS_SUCCEEDED(rv))
        rv = compMgr->CreateInstance(setupCI->classid, nsnull, setupCI->iid, (void **) &instance);

      break;
    }

    // CreateInstanceByContractID
    case DCON_OP_SETUP_NEW_INST_CONTRACTID:
    {
      const DConnectSetupContractID *setupCI = (const DConnectSetupContractID *) setup;

      nsCOMPtr<nsIComponentManager> compMgr;
      rv = NS_GetComponentManager(getter_AddRefs(compMgr));
      if (NS_SUCCEEDED(rv))
        rv = compMgr->CreateInstanceByContractID(setupCI->contractid, nsnull, setupCI->iid, (void **) &instance);

      break;
    }

    // GetService
    case DCON_OP_SETUP_GET_SERV_CLASSID:
    {
      const DConnectSetupClassID *setupCI = (const DConnectSetupClassID *) setup;

      nsCOMPtr<nsIServiceManager> svcMgr;
      rv = NS_GetServiceManager(getter_AddRefs(svcMgr));
      if (NS_SUCCEEDED(rv))
        rv = svcMgr->GetService(setupCI->classid, setupCI->iid, (void **) &instance);
      break;
    }

    // GetServiceByContractID
    case DCON_OP_SETUP_GET_SERV_CONTRACTID:
    {
      const DConnectSetupContractID *setupCI = (const DConnectSetupContractID *) setup;

      nsCOMPtr<nsIServiceManager> svcMgr;
      rv = NS_GetServiceManager(getter_AddRefs(svcMgr));
      if (NS_SUCCEEDED(rv))
        rv = svcMgr->GetServiceByContractID(setupCI->contractid, setupCI->iid, (void **) &instance);

      break;
    }

    // QueryInterface
    case DCON_OP_SETUP_QUERY_INTERFACE:
    {
      const DConnectSetupQueryInterface *setupQI = (const DConnectSetupQueryInterface *) setup;

      // make sure we've been sent a valid wrapper
      if (!CheckInstanceAndAddRef(setupQI->instance))
      {
        NS_NOTREACHED("instance wrapper not found");
        rv = NS_ERROR_INVALID_ARG;
      }
      else
      {
        rv = setupQI->instance->RealInstance()->QueryInterface(setupQI->iid, (void **) &instance);
        setupQI->instance->Release();
      }
      break;
    }

    default:
      NS_NOTREACHED("unexpected minor opcode");
      rv = NS_ERROR_UNEXPECTED;
      break;
  }

  nsVoidArray wrappers;

  // now, create instance wrapper, and store it in our instances set.
  // this allows us to keep track of object references held on behalf of a
  // particular peer.  we can use this information to cleanup after a peer
  // that disconnects without sending RELEASE messages for its objects.
  DConnectInstance *wrapper = nsnull;
  if (NS_SUCCEEDED(rv))
  {
    nsCOMPtr<nsIInterfaceInfo> iinfo;
    rv = GetInterfaceInfo(setup->iid, getter_AddRefs(iinfo));
    if (NS_SUCCEEDED(rv))
    {
      nsAutoLock lock (mLock);

      // first try to find an existing wrapper for the given object
      if (!FindInstanceAndAddRef(peer, instance, &setup->iid, &wrapper))
      {
        wrapper = new DConnectInstance(peer, iinfo, instance);
        if (!wrapper)
          rv = NS_ERROR_OUT_OF_MEMORY;
        else
        {
          rv = StoreInstance(wrapper);
          if (NS_FAILED(rv))
          {
            delete wrapper;
            wrapper = nsnull;
          }
          else
          {
            // reference the newly created wrapper
            wrapper->AddRef();

            if (!wrappers.AppendElement(wrapper))
            {
              NS_RELEASE(wrapper);
              rv = NS_ERROR_OUT_OF_MEMORY;
            }
          }
        }
      }

      // wrapper remains referenced when passing it to the client
      // (will be released upon DCON_OP_RELEASE). increase the
      // second, IPC-only, reference counter
      wrapper->AddRefIPC();
    }
  }

  NS_IF_RELEASE(instance);

  ipcMessageWriter writer(64);

  DConnectSetupReply msg;
  msg.opcode_major = DCON_OP_SETUP_REPLY;
  msg.opcode_minor = 0;
  msg.request_index = setup->request_index;
  msg.instance = wrapper;
  msg.status = rv;

  writer.PutBytes(&msg, sizeof(msg));

  if (NS_FAILED(rv))
  {
    // try to fetch an nsIException possibly set by one of the setup methods
    // and send it instead of the failed instance (even if it is null)
    nsCOMPtr <nsIExceptionService> es;
    es = do_GetService(NS_EXCEPTIONSERVICE_CONTRACTID, &rv);
    if (NS_SUCCEEDED(rv))
    {
      nsCOMPtr <nsIExceptionManager> em;
      rv = es->GetCurrentExceptionManager (getter_AddRefs (em));
      if (NS_SUCCEEDED(rv))
      {
        nsCOMPtr <nsIException> exception;
        rv = em->GetCurrentException (getter_AddRefs (exception));
        if (NS_SUCCEEDED(rv))
        {
          LOG(("got nsIException instance (%p), will serialize\n",
               (nsIException *)exception));

          rv = SerializeException(writer, peer, exception, wrappers);
        }
      }
    }
    NS_ASSERTION(NS_SUCCEEDED(rv), "failed to get/serialze exception");
  }

  // fire off SETUP_REPLY, don't wait for a response
  if (NS_FAILED(rv))
    rv = IPC_SendMessage(peer, kDConnectTargetID,
                         (const PRUint8 *) &msg, sizeof(msg));
  else
    rv = IPC_SendMessage(peer, kDConnectTargetID,
                         writer.GetBuffer(), writer.GetSize());

  if (NS_FAILED(rv))
  {
    LOG(("unable to send SETUP_REPLY: rv=%x\n", rv));
    ReleaseWrappers(wrappers);
  }
}

void
ipcDConnectService::OnRelease(PRUint32 peer, const DConnectRelease *release)
{
  LOG(("ipcDConnectService::OnRelease [peer=%u instance=%p]\n", 
       peer, release->instance));

  DConnectInstance *wrapper = release->instance;

  nsAutoLock lock (mLock);

  // make sure we've been sent a valid wrapper
  if (mInstanceSet.Contains(wrapper))
  {
    // add a fake reference to hold the wrapper alive
    nsrefcnt count = wrapper->AddRef();

    // release references
    nsrefcnt countIPC = wrapper->ReleaseIPC();
    count = wrapper->Release();

    NS_ASSERTION(count > 0, "unbalanced AddRef()/Release()");

    if (count == 1)
    {
      NS_ASSERTION(countIPC == 0, "unbalanced AddRefIPC()/ReleaseIPC()");

      // we are the last one who holds a (fake) reference, remove the
      // instace from instance maps while still under the lock
      DeleteInstance(wrapper, PR_TRUE /* locked */);

      // leave the lock before calling the destructor because it will release
      // the real object which may need to make asynchronous use our service
      lock.unlock();
      delete wrapper;
    }
    else
    {
      // release the fake reference
      wrapper->Release();
    }
  }
  else
  {
    // it is possible that the client disconnection event handler has released
    // all client instances before the RELEASE message sent by the client gets
    // processed here. Just give a warning
    LOG(("ipcDConnectService::OnRelease: WARNING: instance wrapper not found"));
  }
}

void
ipcDConnectService::OnInvoke(PRUint32 peer, const DConnectInvoke *invoke, PRUint32 opLen)
{
  LOG(("ipcDConnectService::OnInvoke [peer=%u instance=%p method=%u]\n",
      peer, invoke->instance, invoke->method_index));

  DConnectInstance *wrapper = invoke->instance;

  ipcMessageReader reader((const PRUint8 *) (invoke + 1), opLen - sizeof(*invoke));

  const nsXPTMethodInfo *methodInfo;
  nsXPTCVariant *params = nsnull;
  nsIInterfaceInfo *iinfo = nsnull;
  PRUint8 i, paramCount = 0, paramUsed = 0;
  nsresult rv;

  nsCOMPtr <nsIException> exception;
  PRBool got_exception = PR_FALSE;

  // make sure we've been sent a valid wrapper
  if (!CheckInstanceAndAddRef(wrapper))
  {
    NS_NOTREACHED("instance wrapper not found");
    wrapper = nsnull;
    rv = NS_ERROR_INVALID_ARG;
    goto end;
  }

  iinfo = wrapper->InterfaceInfo();

  rv = iinfo->GetMethodInfo(invoke->method_index, &methodInfo);
  if (NS_FAILED(rv))
    goto end;

  paramCount = methodInfo->GetParamCount();

  LOG(("  iface=%p\n", wrapper->RealInstance()));
  LOG(("  name=%s\n", methodInfo->GetName()));
  LOG(("  param-count=%u\n", (PRUint32) paramCount));
  LOG(("  request-index=%d\n", (PRUint32) invoke->request_index));

  params = new nsXPTCVariant[paramCount];
  if (!params)
  {
    rv = NS_ERROR_OUT_OF_MEMORY;
    goto end;
  }

  // setup |params| for xptcall

  for (i=0; i<paramCount; ++i, ++paramUsed)
  {
    const nsXPTParamInfo &paramInfo = methodInfo->GetParam(i);

    // XXX are inout params an issue?

    if (paramInfo.IsIn() && !paramInfo.IsDipper())
      rv = DeserializeParam(reader, paramInfo.GetType(), params[i]);
    else
      rv = SetupParam(paramInfo, params[i]);

    if (NS_FAILED(rv))
      goto end;
  }

  // fixup any interface pointers.  we do this with a second pass so that
  // we can properly handle INTERFACE_IS.
  for (i=0; i<paramCount; ++i)
  {
    const nsXPTParamInfo &paramInfo = methodInfo->GetParam(i);
    const nsXPTType &type = paramInfo.GetType();

    if (paramInfo.IsIn() && type.IsInterfacePointer())
    {
      PtrBits bits = (PtrBits) params[i].ptr;
      if (bits & 0x1)
      {
        // pointer is to a remote object.  we need to build a stub.
        params[i].ptr = (void *) (bits & ~0x1);

        nsID iid;
        rv = GetIIDForMethodParam(iinfo, methodInfo, paramInfo, type,
                                  invoke->method_index, i, params, PR_TRUE, iid);
        if (NS_SUCCEEDED(rv))
        {
          DConnectStub *stub;
          rv = CreateStub(iid, peer, (DConAddr) params[i].ptr, &stub);
          if (NS_SUCCEEDED(rv))
          {
            params[i].val.p = params[i].ptr = stub;
            params[i].SetValIsInterface();
          }
        }
        if (NS_FAILED(rv))
          goto end;
      }
      else if (bits)
      {
        // pointer is to one of our instance wrappers.

        DConnectInstance *wrapper = (DConnectInstance *) params[i].ptr;
        // make sure we've been sent a valid wrapper
        if (!CheckInstanceAndAddRef(wrapper))
        {
          NS_NOTREACHED("instance wrapper not found");
          rv = NS_ERROR_INVALID_ARG;
          goto end;
        }
        params[i].val.p = params[i].ptr = wrapper->RealInstance();
        wrapper->Release();
        // do not mark as an interface -- doesn't need to be freed
      }
      else
      {
        params[i].val.p = params[i].ptr = nsnull;
        // do not mark as an interface -- doesn't need to be freed
      }
    }
  }

  rv = XPTC_InvokeByIndex(wrapper->RealInstance(),
                          invoke->method_index,
                          paramCount,
                          params);

  if (NS_FAILED(rv))
  {
    // try to fetch an nsIException possibly set by the method
    nsresult invoke_rv = rv;
    nsCOMPtr <nsIExceptionService> es;
    es = do_GetService(NS_EXCEPTIONSERVICE_CONTRACTID, &rv);
    if (NS_SUCCEEDED(rv))
    {
      nsCOMPtr <nsIExceptionManager> em;
      rv = es->GetCurrentExceptionManager (getter_AddRefs (em));
      if (NS_SUCCEEDED(rv))
      {
        rv = em->GetCurrentException (getter_AddRefs (exception));
        if (NS_SUCCEEDED(rv))
        {
          LOG(("got nsIException instance (%p), will serialize\n",
               (nsIException *)exception));
          got_exception = PR_TRUE;
          // restore the method's result
          rv = invoke_rv;
        }
      }
    }
  }

end:
  LOG(("sending INVOKE_REPLY: rv=%x\n", rv));
  
  // balance CheckInstanceAndAddRef()
  if (wrapper)
    wrapper->Release();

  ipcMessageWriter writer(64);

  DConnectInvokeReply reply;
  reply.opcode_major = DCON_OP_INVOKE_REPLY;
  reply.opcode_minor = 0;
  reply.request_index = invoke->request_index;
  reply.result = rv;

  writer.PutBytes(&reply, sizeof(reply));

  nsVoidArray wrappers;

  if (got_exception)
  {
    rv = SerializeException(writer, peer, exception, wrappers);
    NS_ASSERTION(NS_SUCCEEDED(rv), "failed to get/serialze exception");
  }
  else if (NS_SUCCEEDED(rv) && params)
  {
    // serialize out-params and retvals
    for (i=0; i<paramCount; ++i)
    {
      const nsXPTParamInfo paramInfo = methodInfo->GetParam(i);

      if (paramInfo.IsRetval() || paramInfo.IsOut())
      {
        const nsXPTType &type = paramInfo.GetType();

        if (type.IsInterfacePointer())
        {
          nsID iid;
          rv = GetIIDForMethodParam(iinfo, methodInfo, paramInfo, type,
                                    invoke->method_index, i, params, PR_TRUE, iid);
          if (NS_SUCCEEDED(rv))
            rv = SerializeInterfaceParam(writer, peer, iid,
                                         (nsISupports *) params[i].val.p, wrappers);

          // mark as an interface to let FinishParam() to release this param
          if (NS_SUCCEEDED(rv))
            params[i].SetValIsInterface();
        }
        else
          rv = SerializeParam(writer, type, params[i]);

        if (NS_FAILED(rv))
        {
          reply.result = rv;
          break;
        }
      }
    }
  }

  if (NS_FAILED(rv))
    rv = IPC_SendMessage(peer, kDConnectTargetID, (const PRUint8 *) &reply, sizeof(reply));
  else
    rv = IPC_SendMessage(peer, kDConnectTargetID, writer.GetBuffer(), writer.GetSize());
  if (NS_FAILED(rv))
  {
    LOG(("unable to send INVOKE_REPLY: rv=%x\n", rv));
    ReleaseWrappers(wrappers);
  }

  if (params)
  {
    for (i=0; i<paramUsed; ++i)
      FinishParam(params[i]);
    delete[] params;
  }
}
