// Creates functions at every address listed in config/missing_functions.txt (vtable slots that
// auto-analysis left undefined), then applies names from config/library.csv.
//
// Run from Ghidra's Script Manager with game.exe open. Add E:\workspace\ei\tools\ghidra as a
// script directory first (Script Manager > Manage Script Directories).
// @category EvilIslands
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;

public class EiCreateFunctions extends GhidraScript {
    private static final Path ROOT = Path.of("E:/workspace/ei");

    @Override
    protected void run() throws Exception {
        int created = 0, existing = 0, failed = 0;
        for (String line : Files.readAllLines(ROOT.resolve("config/missing_functions.txt"))) {
            line = line.trim();
            if (line.isEmpty()) continue;
            Address a = toAddr(Long.parseLong(line, 16));
            if (getFunctionAt(a) != null) { existing++; continue; }
            // A slot may point into the middle of a function Ghidra merged; split it out.
            Function containing = getFunctionContaining(a);
            if (getInstructionAt(a) == null) {
                clearListing(a);
                new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
            }
            if (createFunction(a, null) != null) created++;
            else { failed++; println("could not create function at " + a + (containing != null ? " (inside " + containing.getName() + ")" : "")); }
        }
        println("functions created: " + created + ", already present: " + existing + ", failed: " + failed);

        int renamed = 0;
        List<String> lib = Files.readAllLines(ROOT.resolve("config/library.csv"));
        for (String row : lib.subList(1, lib.size())) {
            String[] f = row.split(",", 3);
            String name = f[1].replace("\"", "").replace(' ', '_');
            Function fn = getFunctionAt(toAddr(Long.parseLong(f[0], 16)));
            if (fn != null && !fn.getName().equals(name)) {
                fn.setName(name, SourceType.USER_DEFINED);
                renamed++;
            }
        }
        println("library names applied: " + renamed);
    }
}
