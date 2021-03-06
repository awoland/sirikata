/*  Sirikata
 *  ObjectHostContext.hpp
 *
 *  Copyright (c) 2009, Ewen Cheslack-Postava
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of Sirikata nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SIRIKATA_OBJECT_HOST_CONTEXT_HPP_
#define _SIRIKATA_OBJECT_HOST_CONTEXT_HPP_

#include <sirikata/oh/Platform.hpp>
#include <sirikata/core/service/Context.hpp>
#include <sirikata/core/odp/SSTDecls.hpp>
#include <sirikata/core/ohdp/SSTDecls.hpp>
#include "Trace.hpp"

namespace Sirikata {

class ObjectHost;

class SIRIKATA_OH_EXPORT ObjectHostContext : public Context {
public:
    ObjectHostContext(const String& name, ObjectHostID _id, ODPSST::ConnectionManager* sstConnMgr, OHDPSST::ConnectionManager* ohSstConnMgr, Network::IOService* ios, Network::IOStrand* strand, Trace::Trace* _trace, const Time& epoch, const Duration& simlen = Duration::zero());
    ~ObjectHostContext();

    ObjectHostID id;
    ObjectHost* objectHost;
    OHTrace* ohtrace() const { return mOHTrace; }
    const String& name() { return mName; }
    ODPSST::ConnectionManager* sstConnMgr() { return mSSTConnMgr; }
    OHDPSST::ConnectionManager* ohSSTConnMgr() { return mOHSSTConnMgr; }


private:
    const String mName;
    OHTrace* mOHTrace;
    ODPSST::ConnectionManager* mSSTConnMgr;
    OHDPSST::ConnectionManager* mOHSSTConnMgr;

}; // class ObjectHostContext

} // namespace Sirikata


#endif //_SIRIKATA_OBJECT_HOST_CONTEXT_HPP_
