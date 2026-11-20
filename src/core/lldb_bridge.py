"""LLDB adapter for CodeLens. Exchanges JSON messages over the debugger's pipes."""
import json
import os
import re
import time
import threading
import lldb


def emit(data):
    os.write(1, ("CODELENS_JSON " + json.dumps(data) + "\n").encode())


def lines():
    pending = b""
    while True:
        chunk = os.read(0, 65536)
        if not chunk:
            return
        pending += chunk
        while b"\n" in pending:
            line, pending = pending.split(b"\n", 1)
            yield json.loads(line)


class Session:
    def __init__(self):
        self.debugger = lldb.SBDebugger.Create()
        self.debugger.SetAsync(False)
        self.target = None
        self.process = None
        self.directory = ""
        self.file = ""
        self.source = []
        self.previous = {}
        self.unknown = {}
        self.timed_out = False

    def wait(self, timeout=3):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            state = self.process.GetState()
            if state in (lldb.eStateStopped, lldb.eStateCrashed, lldb.eStateExited):
                return
            time.sleep(0.01)
        self.process.Stop()
        self.timed_out = True
        for _ in range(100):
            if self.process.GetState() == lldb.eStateStopped:
                return
            time.sleep(0.01)

    def start(self, request):
        self.directory = request["directory"]
        self.file = request["file"]
        self.source = request["code"].splitlines()
        self.target = self.debugger.CreateTarget(self.directory + "/program")
        self.target.BreakpointCreateByName("main")
        launch = lldb.SBLaunchInfo([])
        launch.SetWorkingDirectory(self.directory)
        launch.SetLaunchFlags(launch.GetLaunchFlags() & ~lldb.eLaunchFlagDisableASLR)
        launch.AddOpenFileAction(0, self.directory + "/stdin.txt", True, False)
        launch.AddOpenFileAction(1, self.directory + "/out.txt", False, True)
        launch.AddOpenFileAction(2, self.directory + "/err.txt", False, True)
        error = lldb.SBError()
        self.process = self.target.Launch(launch, error)
        if error.Fail():
            return {"error": error.GetCString()}
        self.wait(8)
        self.target.DeleteAllBreakpoints()
        result = self.snapshot()
        result["pid"] = self.process.GetProcessID()
        return result

    def user_frames(self):
        thread = self.process.GetSelectedThread()
        return [frame for frame in thread
                if frame.GetLineEntry().GetFileSpec().GetFilename() == self.file]

    def variable(self, value, depth=0, name=None):
        typ = value.GetType()
        address = value.GetLoadAddress()
        if address == lldb.LLDB_INVALID_ADDRESS:
            address = 0
        result = {"name": name or value.GetName() or "", "type": value.GetTypeName() or "",
                  "value": value.GetValue() or value.GetSummary() or "",
                  "address": hex(address)}
        if typ.IsPointerType():
            result.update(is_pointer=True, points_to=hex(value.GetValueAsUnsigned()))
            if typ.GetPointeeType().GetBasicType() == lldb.eBasicTypeChar and value.GetSummary():
                result["value"] += " " + value.GetSummary()
        elif depth < 4 and value.GetNumChildren() > 0:
            # Preserve character strings; expose other arrays and structs recursively.
            if typ.IsArrayType() and typ.GetArrayElementType().GetBasicType() == lldb.eBasicTypeChar:
                result["value"] = value.GetSummary() or ""
            else:
                result["value"] = ""
                result["children"] = [self.variable(value.GetChildAtIndex(i), depth + 1)
                                      for i in range(min(value.GetNumChildren(), 64))]
        return result

    def heap(self, frames):
        count = self.target.FindFirstGlobalVariable("cl_heap_n").GetValueAsUnsigned()
        array = self.target.FindFirstGlobalVariable("cl_heap")
        blocks = []
        for i in range(min(count, 256)):
            block = array.GetChildAtIndex(i)
            address = block.GetChildMemberWithName("addr").GetValueAsUnsigned()
            size = block.GetChildMemberWithName("size").GetValueAsUnsigned()
            freed = bool(block.GetChildMemberWithName("freed").GetValueAsUnsigned())
            cells = []
            if not freed:
                for frame in frames:
                    for pointer in frame.GetVariables(True, True, False, True):
                        typ = pointer.GetType()
                        if not typ.IsPointerType() or pointer.GetValueAsUnsigned() != address:
                            continue
                        element = typ.GetPointeeType()
                        element_size = element.GetByteSize()
                        if not element_size or element.GetBasicType() == lldb.eBasicTypeVoid:
                            continue
                        for index in range(min(size // element_size, 32)):
                            cell = self.target.CreateValueFromAddress(
                                "[" + str(index) + "]",
                                lldb.SBAddress(address + index * element_size, self.target), element)
                            cells.append(self.variable(cell))
                        break
                    if cells:
                        break
            blocks.append(dict(address=hex(address), size=size, freed=freed, cells=cells))
        return blocks

    def snapshot(self):
        finished = self.process.GetState() == lldb.eStateExited
        state = dict(stack=[], heap=[], output="", errors=[], line=0, finished=finished, exit_code=0)
        try:
            with open(self.directory + "/out.txt") as output:
                state["output"] = output.read()
        except OSError:
            pass
        if finished:
            state["exit_code"] = self.process.GetExitStatus()
            try:
                with open(self.directory + "/cl_heap.txt") as final_heap:
                    for row in final_heap:
                        address, size, freed = row.split()[:3]
                        state["heap"].append(dict(address=address, size=int(size), freed=freed == "1"))
            except OSError:
                state["heap"] = self.previous.get("heap", [])
            for block in state["heap"]:
                if not block["freed"]:
                    state["errors"].append(dict(kind="memory-leak", line=0,
                                                details=str(block["size"]) + " bytes never freed"))
            state["note"] = "The program finished."
        else:
            frames = self.user_frames()
            for depth, frame in enumerate(reversed(frames)):
                line = frame.GetLineEntry().GetLine()
                variables = []
                previous_frame = self.previous.get("stack", [])
                previous_names = {}
                if depth < len(previous_frame) and previous_frame[depth]["function"] == frame.GetFunctionName():
                    previous_names = {v["name"]: v for v in previous_frame[depth]["vars"]}
                for value in frame.GetVariables(True, True, False, True):
                    name = value.GetName()
                    declaration = value.GetDeclaration().GetLine()
                    argument = value.GetValueType() == lldb.eValueTypeVariableArgument
                    if not argument and declaration >= line and name not in previous_names:
                        continue
                    variable = self.variable(value)
                    key = str(depth) + "#" + name
                    declaration_text = self.source[declaration - 1] if 0 < declaration <= len(self.source) else ""
                    if name not in previous_names and not argument and "=" not in declaration_text:
                        self.unknown[key] = variable["value"]
                    if key in self.unknown:
                        if variable["value"] == self.unknown[key]:
                            variable["value"] = "?"
                        else:
                            del self.unknown[key]
                    variables.append(variable)
                state["stack"].append(dict(function=frame.GetFunctionName(), line=line, vars=variables))
            if frames:
                state["line"] = frames[0].GetLineEntry().GetLine()
            state["heap"] = self.heap(frames)
            thread = self.process.GetSelectedThread()
            reason = thread.GetStopReason()
            if reason in (lldb.eStopReasonException, lldb.eStopReasonSignal):
                description = thread.GetStopDescription(512) or ""
                state["finished"] = True
                state["errors"].append(dict(kind="SEGV", line=state["line"],
                                            details="Segmentation fault: the program used an invalid pointer"))
                state["note"] = "Segmentation fault on line " + str(state["line"]) + ". " + description
        if self.timed_out:
            state["finished"] = True
            state["note"] = "This line did not finish in 3 seconds. Is there an infinite loop?"
        self.previous = state
        return {"state": state}

    def interrupt(self):
        if self.process and self.process.GetState() == lldb.eStateRunning:
            self.timed_out = True
            self.process.Stop()

    def execute(self, request):
        action = request["action"]
        if action == "start":
            return self.start(request)
        if action == "eval":
            value = self.process.GetSelectedThread().GetFrameAtIndex(0).EvaluateExpression(request["expression"])
            if value.GetError().Fail():
                return {"value": ""}
            raw = value.GetValue() or value.GetSummary() or ""
            if value.GetType().GetBasicType() in (lldb.eBasicTypeChar, lldb.eBasicTypeSignedChar):
                number = value.GetValueAsSigned()
                raw = str(number) + " '" + chr(number) + "'"
            return {"value": raw}
        if action == "stop":
            self.process.Kill()
            return {}
        thread = self.process.GetSelectedThread()
        timer = threading.Timer(3.0, self.interrupt)
        timer.daemon = True
        timer.start()
        if action == "line":
            self.target.BreakpointCreateByLocation(self.file, request["line"])
            self.process.Continue()
        elif action == "-exec-step":
            thread.StepInto()
        elif action == "-exec-finish":
            thread.StepOut()
        elif action == "-exec-continue":
            self.process.Continue()
        else:
            thread.StepOver()
        timer.cancel()
        self.wait()
        self.target.DeleteAllBreakpoints()
        # Step out can land in a system return trampoline.
        for _ in range(8):
            if self.process.GetState() == lldb.eStateExited or self.user_frames() or self.timed_out:
                break
            self.process.Continue()
            self.wait()
        return self.snapshot()


def run():
    session = Session()
    for request in lines():
        try:
            emit(session.execute(request))
        except Exception as error:
            emit({"error": str(error)})
        if request.get("action") == "stop":
            break
