/*  Sirikata
 *  Options.cpp
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

#include <sirikata/cbrcore/Options.hpp>
#include <sirikata/core/options/Options.hpp>
#include <sirikata/core/util/Time.hpp>


namespace Sirikata {

void InitOptions() {
    InitializeClassOptions::module(SIRIKATA_OPTIONS_MODULE)
        .addOption( reinterpret_cast<Sirikata::OptionValue*>(Sirikata_Logging_OptionValue_defaultLevel) )
        .addOption( reinterpret_cast<Sirikata::OptionValue*>(Sirikata_Logging_OptionValue_atLeastLevel) )
        .addOption( reinterpret_cast<Sirikata::OptionValue*>(Sirikata_Logging_OptionValue_moduleLevel) )

        .addOption(new OptionValue("ohstreamlib","tcpsst",Sirikata::OptionValueType<String>(),"Which library to use to communicate with the object host"))
        .addOption(new OptionValue("ohstreamoptions","--send-buffer-size=32768 --parallel-sockets=1 --no-delay=true",Sirikata::OptionValueType<String>(),"TCPSST stream options such as how many bytes to collect for sending during an ongoing asynchronous send call."))

        .addOption(new OptionValue("falloff", "sqr", Sirikata::OptionValueType<String>(), "Type of communication falloff function to use.  Valid values are sqr and guassian. Default is sqr."))
        .addOption(new OptionValue("flatness", "8", Sirikata::OptionValueType<double>(), "k where e^-kx is the bandwidth function and x is the distance between 2 server points"))
        .addOption(new OptionValue("const-cutoff", "64", Sirikata::OptionValueType<double>(), "cutoff below with a constant bandwidth is used"))

        .addOption(new OptionValue("region", "<<-100,-100,-100>,<100,100,100>>", Sirikata::OptionValueType<BoundingBox3f>(), "Simulation region"))
        .addOption(new OptionValue("layout", "<2,1,1>", Sirikata::OptionValueType<Vector3ui32>(), "Layout of servers in uniform grid - ixjxk servers"))
        .addOption(new OptionValue("max-servers", "0", Sirikata::OptionValueType<uint32>(), "Maximum number of servers available for the simulation; if set to 0, use the number of servers specified in the layout option"))
        .addOption(new OptionValue("duration", "1s", Sirikata::OptionValueType<Duration>(), "Duration of the simulation"))
        .addOption(new OptionValue("serverips", "serverip.txt", Sirikata::OptionValueType<String>(), "The file containing the server ip list"))

        .addOption(new OptionValue("capexcessbandwidth", "false", Sirikata::OptionValueType<bool>(), "Total bandwidth for this server in bytes per second"))

        .addOption(new OptionValue("rand-seed", "0", Sirikata::OptionValueType<uint32>(), "The random seed to synchronize all servers"))

        .addOption(new OptionValue(STATS_TRACE_FILE, "trace.txt", Sirikata::OptionValueType<String>(), "The filename to save the trace to"))

        .addOption(new OptionValue("time-server", "", Sirikata::OptionValueType<String>(), "The server to sync with"))
        .addOption(new OptionValue("wait-until","",Sirikata::OptionValueType<String>(),"The date to wait until before starting"))
        .addOption(new OptionValue("wait-additional","0s",Sirikata::OptionValueType<Duration>(),"How much additional time after date has passed to wait until before starting"))

        .addOption(new OptionValue("monitor-load", "false", Sirikata::OptionValueType<bool>(), "Does the LoadMonitor monitor queue sizes?"))


        .addOption(new OptionValue(PROFILE, "false", Sirikata::OptionValueType<bool>(), "Whether to report profiling information."))
      ;
}

void ParseOptions(int argc, char** argv) {
    OptionSet* options = OptionSet::getOptions(SIRIKATA_OPTIONS_MODULE,NULL);
    options->parse(argc, argv);
}

OptionValue* GetOption(const char* name) {
    OptionSet* options = OptionSet::getOptions(SIRIKATA_OPTIONS_MODULE,NULL);
    return options->referenceOption(name);
}

// FIXME method naming
String GetPerServerString(const String& orig, const ServerID& sid) {
    int32 dot_pos = orig.rfind(".");
    String prefix = orig.substr(0, dot_pos);
    String ext = orig.substr(dot_pos+1);

    char buffer[1024];
    sprintf(buffer, "%s-%04d.%s", prefix.c_str(), (uint32)sid, ext.c_str());

    return buffer;
}

String GetPerServerFile(const char* opt_name, const ServerID& sid) {
    String orig = GetOption(opt_name)->as<String>();
    return GetPerServerString(orig, sid);
}

String GetPerServerFile(const char* opt_name, const ObjectHostID& ohid) {
    return (GetPerServerFile(opt_name, (ServerID)ohid.id)); // FIXME relies on fact that ServerID and ObjectHostID are both uint64
}

} // namespace Sirikata