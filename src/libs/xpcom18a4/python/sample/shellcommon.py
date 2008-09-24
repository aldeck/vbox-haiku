import traceback
import sys
import pdb

g_hasreadline = 1
try:
    import readline
    import rlcompleter
except:
    g_hasreadline = 0


if g_hasreadline:
  class CompleterNG(rlcompleter.Completer):
    def __init__(self, dic, ctx):
        self.ctx = ctx
        return rlcompleter.Completer.__init__(self,dic)

    def complete(self, text, state):
        """
        taken from:
        http://aspn.activestate.com/ASPN/Cookbook/Python/Recipe/496812
        """
        if text == "":
            return ['\t',None][state]
        else:
            return rlcompleter.Completer.complete(self,text,state)

    def global_matches(self, text):
        """
        Compute matches when text is a simple name.
        Return a list of all names currently defined
        in self.namespace that match.
        """
        
        matches = []
        n = len(text)

        for list in [ self.namespace ]:
            for word in list:
                if word[:n] == text:
                    matches.append(word)


        try:
            for m in getMachines(self.ctx):
                # although it has autoconversion, we need to cast 
                # explicitly for subscripts to work
                word = str(m.name)
                if word[:n] == text:
                    matches.append(word)
                word = str(m.id)
                if word[0] == '{':
                    word = word[1:-1]
                if word[:n] == text:
                    matches.append(word)
        except Exception,e:
            traceback.print_exc()
            print e

        return matches


def autoCompletion(commands, ctx):
  if  not g_hasreadline:
      return
  
  comps = {}
  for (k,v) in commands.items():
      comps[k] = None
  completer = CompleterNG(comps, ctx)
  readline.set_completer(completer.complete)
  readline.parse_and_bind("tab: complete")

g_verbose = True

def split_no_quotes(s):
   return s.split()
   
def startVm(mgr,vb,mach,type):
    session = mgr.getSessionObject(vb)
    uuid = mach.id
    progress = vb.openRemoteSession(session, uuid, type, "")
    progress.waitForCompletion(-1)
    completed = progress.completed
    rc = progress.resultCode
    print "Completed:", completed, "rc:",rc
    if rc == 0:
        vb.performanceCollector.setupMetrics(['*'], [mach], 10, 15)
    session.close()

def getMachines(ctx):
    return ctx['vb'].getMachines2()

def asState(var):
    if var:
        return 'on'
    else:
        return 'off'

def guestStats(ctx,mach):
    collector = ctx['vb'].performanceCollector
    (vals, names, objs, idxs, lens) = collector.queryMetricsData(["*"], [mach])
    for i in range(0,len(names)):
        valsStr = '[ '
        for j in range(0, lens[i]):
            valsStr += str(vals[idxs[i]])+' '
        valsStr += ']'
        print names[i],valsStr

def cmdExistingVm(ctx,mach,cmd):
    mgr=ctx['mgr']
    vb=ctx['vb']
    session = mgr.getSessionObject(vb)
    uuid = mach.id
    try:
        progress = vb.openExistingSession(session, uuid)
    except Exception,e:
        print "Session to '%s' not open: %s" %(mach.name,e)
        if g_verbose:
            traceback.print_exc()
        return
    if session.state != ctx['ifaces'].SessionState.Open:
        print "Session to '%s' in wrong state: %s" %(mach.name, session.state)
        return
    # unfortunately IGuest is suppressed, thus WebServices knows not about it
    # this is an example how to handle local only functionality
    if ctx['remote'] and cmd == 'stats2':
        print 'Trying to use local only functionality, ignored'
        return        
    console=session.console
    ops={'pause' :     lambda: console.pause(),
         'resume':     lambda: console.resume(),
         'powerdown':  lambda: console.powerDown(),
         'stats':      lambda: guestStats(ctx, mach),
         }
    ops[cmd]()
    session.close()

# can cache known machines, if needed
def machById(ctx,id):
    mach = None
    for m in getMachines(ctx):
        if m.name == id:
            mach = m
            break
        mid = str(m.id)
        if mid[0] == '{':
            mid = mid[1:-1]
        if mid == id:
            mach = m
            break
    return mach

def argsToMach(ctx,args):
    if len(args) < 2:
        print "usage: %s [vmname|uuid]" %(args[0])
        return None
    id = args[1]
    m = machById(ctx, id)
    if m == None:
        print "Machine '%s' is unknown, use list command to find available machines" %(id)
    return m

def helpCmd(ctx, args):
    if len(args) == 1:
        print "Help page:"
        for i in commands:
            print "   ",i,":", commands[i][0] 
    else:
        c = commands.get(args[1], None)
        if c == None:
            print "Command '%s' not known" %(args[1])
        else:
            print "   ",args[1],":", c[0]
    return 0

def listCmd(ctx, args):
    for m in getMachines(ctx):
        print "Machine '%s' [%s], state=%s" %(m.name,m.id,m.sessionState)
    return 0

def infoCmd(ctx,args):
    if (len(args) < 2):
        print "usage: info [vmname|uuid]"
        return 0
    mach = argsToMach(ctx,args) 
    if mach == None:
        return 0
    os = ctx['vb'].getGuestOSType(mach.OSTypeId)
    print "  Name: ",mach.name
    print "  ID: ",mach.id
    print "  OS Type: ",os.description
    print "  RAM:  %dM" %(mach.memorySize)
    print "  VRAM:  %dM" %(mach.VRAMSize)
    print "  Monitors:  %d" %(mach.MonitorCount)
    print "  Clipboard mode:  %d" %(mach.clipboardMode)
    print "  Machine status: " ,mach.sessionState
    bios = mach.BIOSSettings
    print "  BIOS ACPI: ",bios.ACPIEnabled
    print "  PAE: ",mach.PAEEnabled
    print "  Hardware virtualization: ",asState(mach.HWVirtExEnabled)
    print "  Nested paging: ",asState(mach.HWVirtExNestedPagingEnabled)
    print "  Last changed: ",mach.lastStateChange

    return 0 

def startCmd(ctx, args):
    mach = argsToMach(ctx,args) 
    if mach == None:
        return 0
    if len(args) > 2:
        type = args[2]
    else:
        type = "gui"
    startVm(ctx['mgr'], ctx['vb'], mach, type)
    return 0

def pauseCmd(ctx, args):
    mach = argsToMach(ctx,args) 
    if mach == None:
        return 0
    cmdExistingVm(ctx, mach, 'pause')
    return 0

def powerdownCmd(ctx, args):
    mach = argsToMach(ctx,args) 
    if mach == None:
        return 0
    cmdExistingVm(ctx, mach, 'powerdown')
    return 0

def resumeCmd(ctx, args):
    mach = argsToMach(ctx,args) 
    if mach == None:
        return 0
    cmdExistingVm(ctx, mach, 'resume')
    return 0

def statsCmd(ctx, args):
    mach = argsToMach(ctx,args) 
    if mach == None:
        return 0
    cmdExistingVm(ctx, mach, 'stats')
    return 0

def setvarCmd(ctx, args):
    if (len(args) < 4):
        print "usage: setvar [vmname|uuid] expr value"
        return 0
    mach = argsToMach(ctx,args) 
    if mach == None:
        return 0
    vbox = ctx['vb']
    session = ctx['mgr'].getSessionObject(vbox)
    vbox.openSession(session, mach.id)
    mach = session.machine
    expr = 'mach.'+args[2]+' = '+args[3]
    print "Executing",expr
    try:
        exec expr
    except Exception, e:
        print 'failed: ',e
        if g_verbose:
            traceback.print_exc()
    mach.saveSettings()
    session.close()
    return 0

def quitCmd(ctx, args):
    return 1

def aliasesCmd(ctx, args):
    for (k,v) in aliases.items(): 
        print "'%s' is an alias for '%s'" %(k,v)
    return 0

def verboseCmd(ctx, args):
    global g_verbose
    g_verbose = not g_verbose
    return 0

def hostCmd(ctx, args):
   host = ctx['vb'].host
   cnt = host.processorCount
   print "Processor count:",cnt
   for i in range(0,cnt):
      print "Processor #%d speed: %dMHz" %(i,host.getProcessorSpeed(i))
                
   collector = ctx['vb'].performanceCollector
  
   (vals, names, objs, idxs, lens) = collector.queryMetricsData(["*"], [host])
   for i in range(0,len(names)):
       valsStr = '[ '
       for j in range(0, lens[i]):
           valsStr += str(vals[idxs[i]])+' '
       valsStr += ']'
       print names[i],valsStr

   return 0


def evalCmd(ctx, args):
   expr = ' '.join(args[1:])
   try:
        exec expr
   except Exception, e:
        print 'failed: ',e
        if g_verbose:
            traceback.print_exc()
   return 0

aliases = {'s':'start',  
           'i':'info',
           'l':'list',
           'h':'help',
           'a':'aliases',
           'q':'quit', 'exit':'quit',
           'v':'verbose'}

commands = {'help':['Prints help information', helpCmd],
            'start':['Start virtual machine by name or uuid', startCmd],
            'pause':['Pause virtual machine', pauseCmd],
            'resume':['Resume virtual machine', resumeCmd],
            'stats':['Stats for virtual machine', statsCmd],
            'powerdown':['Power down virtual machine', powerdownCmd],
            'list':['Shows known virtual machines', listCmd],
            'info':['Shows info on machine', infoCmd],
            'aliases':['Shows aliases', aliasesCmd],
            'verbose':['Toggle verbosity', verboseCmd],
            'setvar':['Set VMs variable: setvar Fedora BIOSSettings.ACPIEnabled True', setvarCmd],
            'eval':['Evaluate arbitrary Python construction: eval for m in getMachines(ctx): print m.name,"has",m.memorySize,"M"', evalCmd],
            'quit':['Exits', quitCmd],
            'host':['Show host information', hostCmd]}

def runCommand(ctx, cmd):
    if len(cmd) == 0: return 0 
    args = split_no_quotes(cmd)
    c = args[0]
    if aliases.get(c, None) != None:
        c = aliases[c]
    ci = commands.get(c,None)
    if ci == None:
        print "Unknown command: '%s', type 'help' for list of known commands" %(c)
        return 0
    return ci[1](ctx, args)


def interpret(ctx):
    vbox = ctx['vb']
    print "Running VirtualBox version %s" %(vbox.version)

    autoCompletion(commands, ctx)

    # to allow to print actual host information, we collect info for
    # last 150 secs maximum, (sample every 10 secs and keep up to 15 samples)
    vbox.performanceCollector.setupMetrics(['*'], [vbox.host], 10, 15)
   
    while True:
        try:
            cmd = raw_input("vbox> ")
            done = runCommand(ctx, cmd)
            if done != 0: break
        except KeyboardInterrupt:            
            print '====== You can type quit or q to leave'
            break
        except EOFError:
            break;
        except Exception,e:
            print e
            if g_verbose:
                traceback.print_exc()

    vbox.performanceCollector.disableMetrics(['*'], [vbox.host])
