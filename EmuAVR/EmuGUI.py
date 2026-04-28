import sys
import os
import json
import socket
import threading
import subprocess
import time
from queue import Queue, Empty

from PyQt5.QtCore import Qt, QTimer, pyqtSignal, QObject
from PyQt5.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
                             QPushButton, QLabel, QFileDialog, QTextEdit, QTableWidget,
                             QTableWidgetItem, QSpinBox, QLineEdit, QMessageBox, QGroupBox)


class JsonSocketReader(threading.Thread):
    def __init__(self, host, port, out_queue, stop_event):
        super().__init__(daemon=True)
        self.host = host
        self.port = port
        self.out_queue = out_queue
        self.stop_event = stop_event
        self.sock = None
        self.file = None

    def run(self):
        # --- Retry loop to wait for C++ to boot up ---
        connected = False
        while not self.stop_event.is_set() and not connected:
            try:
                self.sock = socket.create_connection((self.host, self.port), timeout=2.0)
                connected = True
            except (ConnectionRefusedError, socket.timeout, OSError):
                # Server isn't awake yet, sleep for 0.5s and try again
                time.sleep(0.5)

        if self.stop_event.is_set():
            return

        try:
            self.file = self.sock.makefile('r', encoding='utf-8', newline='\n')
            self.out_queue.put({"__meta__": "connected"})
            
            while not self.stop_event.is_set():
                line = self.file.readline()
                if not line:
                    break
                line = line.strip()
                if line:
                    try:
                        obj = json.loads(line)
                        self.out_queue.put(obj)
                    except Exception:
                        self.out_queue.put({"__raw__": line})
        except Exception as e:
            self.out_queue.put({"__meta__": "connect_error", "error": str(e)})
        finally:
            if self.file: self.file.close()
            if self.sock: self.sock.close()
            self.file = None
            self.sock = None


class EventPump(QObject):
    json_received = pyqtSignal(dict)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("EmuAVR GUI - Realtime Monitor")
        self.resize(1250, 800)

        self.regs = [0] * 32
        self.PC = 0
        self.SP = 0x085F
        self.SREG = 0
        self.sram = {}
        self.mem_page_base = 0x0060
        self.page_size = 20
        
        self.socket_thread = None
        self.socket_queue = Queue()
        self.socket_stop = threading.Event()
        self.emulator_process = None
        self.emulator_queue = Queue()

        self.event_pump = EventPump()
        self.event_pump.json_received.connect(self.handle_json_event)

        self._build_ui()
        
        # Faster polling (20ms) to ensure we catch rapid bursts of data
        self.poll_timer = QTimer(self)
        self.poll_timer.timeout.connect(self._poll_queues)
        self.poll_timer.start(20)

    def closeEvent(self, event):
        # Kill the emulator if the GUI is closed
        if self.emulator_process and self.emulator_process.poll() is None:
            self.emulator_process.kill()
        self.socket_stop.set()
        event.accept()
        
    def _build_ui(self):
        main = QWidget()
        self.setCentralWidget(main)
        v = QVBoxLayout(main)

        top_h = QHBoxLayout()
        v.addLayout(top_h)

        control_group = QGroupBox("Control")
        control_layout = QHBoxLayout()
        control_group.setLayout(control_layout)
        self.file_path_label = QLineEdit()
        self.file_path_label.setPlaceholderText("Select HEX/C file -->")
        control_layout.addWidget(self.file_path_label)
        btn_file = QPushButton("Hex/C")
        btn_file.clicked.connect(self.launch_emulator)
        control_layout.addWidget(btn_file)
        self.clear_memory_btn = QPushButton("Clear All Memory")
        self.clear_memory_btn.setStyleSheet("background-color: #ffcccc; font-weight: bold;")
        self.clear_memory_btn.clicked.connect(self.clear_all_memory)
        control_layout.addWidget(self.clear_memory_btn)
        control_layout.addStretch()
        top_h.addWidget(control_group, 1)

        mid_h = QHBoxLayout()
        v.addLayout(mid_h, 1)

        # Left: Registers
        regs_group = QGroupBox("CPU Registers")
        regs_layout = QVBoxLayout(regs_group)
        self.reg_table = QTableWidget(16, 2)
        self.reg_table.setHorizontalHeaderLabels(["R0..R15", "R16..R31"])
        self.reg_table.verticalHeader().setVisible(False)
        regs_layout.addWidget(self.reg_table)
        
        state_h = QHBoxLayout()
        state_h.addWidget(QLabel("PC:"))
        self.pc_lbl = QLabel("0x0000")
        self.pc_lbl.setStyleSheet("font-weight: bold; color: blue; font-family: monospace; font-size: 14px;")
        state_h.addWidget(self.pc_lbl)
        state_h.addWidget(QLabel("SP:"))
        self.sp_lbl = QLabel("0x0000")
        self.sp_lbl.setStyleSheet("font-weight: bold; color: green; font-family: monospace; font-size: 14px;")
        state_h.addWidget(self.sp_lbl)
        state_h.addWidget(QLabel("SREG:"))
        self.sreg_lbl = QLabel("0x00")
        self.sreg_lbl.setStyleSheet("font-weight: bold; color: red; font-family: monospace; font-size: 14px;")
        state_h.addWidget(self.sreg_lbl)
        state_h.addStretch()
        state_h.addWidget(QLabel("LED (PB5):"))
        self.led_indicator = QLabel()
        self.led_indicator.setFixedSize(20, 20)
        # Start in "OFF" state (Dark Red)
        self.led_indicator.setStyleSheet("background-color: #550000; border-radius: 10px; border: 2px solid #220000;")
        state_h.addWidget(self.led_indicator)
        regs_layout.addLayout(state_h)
        mid_h.addWidget(regs_group, 1)

        # Right: Memory & Logs
        right_v = QVBoxLayout()
        mid_h.addLayout(right_v, 2)

        mem_group = QGroupBox("SRAM (Live View)")
        mem_layout = QVBoxLayout(mem_group)
        self.mem_grid = QTableWidget(4, 5)
        mem_layout.addWidget(self.mem_grid)
        
        pag_h = QHBoxLayout()
        self.page_base_edit = QLineEdit(f"{self.mem_page_base:04X}")
        self.page_base_edit.setFixedWidth(60)
        pag_h.addWidget(QLabel("Base: 0x"))
        pag_h.addWidget(self.page_base_edit)
        btn_set = QPushButton("Set")
        btn_set.clicked.connect(self.set_page_base)
        pag_h.addWidget(btn_set)
        btn_prev = QPushButton("◄")
        btn_prev.clicked.connect(self.page_prev)
        pag_h.addWidget(btn_prev)
        btn_next = QPushButton("►")
        btn_next.clicked.connect(self.page_next)
        pag_h.addWidget(btn_next)
        mem_layout.addLayout(pag_h)
        right_v.addWidget(mem_group, 1)

        log_group = QGroupBox("Event Log")
        log_layout = QVBoxLayout(log_group)
        self.log_view = QTextEdit()
        self.log_view.setReadOnly(True)
        log_layout.addWidget(self.log_view)
        btn_clear = QPushButton("Clear Log")
        btn_clear.clicked.connect(self.log_view.clear)
        log_layout.addWidget(btn_clear)
        right_v.addWidget(log_group, 2)

        self._update_reg_table()
        self._update_mem_grid()
        self._update_pc_sp_sreg()

    def choose_input_file(self):
        path, _ = QFileDialog.getOpenFileName(self, "Select HEX or C File", "", "*.hex;*.c")
        if path:
            self.file_path_label.setText(path)

    def launch_emulator(self):
        """Prompt for file selection and start the emulator."""
        self.choose_input_file()
        hex_path = self.file_path_label.text().strip()
        if hex_path:
            self._start_emulator()

    def _start_emulator(self):
        """Load and start the emulator with the selected file."""
        exe_path = "./StartUp/EmuAVR.exe" 
        
        hex_path = self.file_path_label.text().strip()
        if not hex_path:
            self.log_view.append(f"<font color='red'>[Error] No hex file selected</font>")
            return

        exe_full = os.path.abspath(exe_path)
        hex_full = os.path.abspath(hex_path)

        if not os.path.isfile(exe_full):
            self.log_view.append(f"<font color='red'>[Error] Emulator not found: {exe_full}</font>")
            return

        if not os.path.isfile(hex_full):
            self.log_view.append(f"<font color='red'>[Error] Hex file not found: {hex_full}</font>")
            return

        self._connect_socket()

        try:
            with open("EmuAVR_pending.txt", "w") as f:
                f.write(hex_full)
            
            self.emulator_queue = Queue()
            self.emulator_process = subprocess.Popen(
                [exe_full, "--socket-wait"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
            )
            threading.Thread(target=self._read_emulator_output, daemon=True).start()
            self.log_view.append(f"<font color='green'>[GUI] Emulator started from {exe_full}</font>")
            self.log_view.append(f"<font color='green'>[GUI] Loaded program: {hex_full}</font>")
        except Exception as e:
            self.log_view.append(f"<font color='red'>[Error] Failed to start emulator: {str(e)}</font>")

    def _connect_socket(self):
        """Establish socket connection to emulator."""
        if not self.socket_thread or not self.socket_thread.is_alive():
            self.socket_stop.clear()
            self.socket_thread = JsonSocketReader("127.0.0.1", 5555, self.socket_queue, self.socket_stop)
            self.socket_thread.start()

    def _read_emulator_output(self):
        while self.emulator_process.poll() is None:
            line = self.emulator_process.stdout.readline()
            if line: self.emulator_queue.put(("stdout", line.strip()))
        
        # Flush remaining
        out, err = self.emulator_process.communicate()
        for l in out.splitlines(): self.emulator_queue.put(("stdout", l))
        for l in err.splitlines(): self.emulator_queue.put(("stderr", l))

    def clear_all_memory(self):
        """Clear CPU registers and SRAM."""
        reply = QMessageBox.question(
            self, "Clear All Memory", 
            "This will reset all CPU registers and SRAM. Continue?",
            QMessageBox.Yes | QMessageBox.No
        )
        if reply == QMessageBox.Yes:
            self.regs = [0] * 32
            self.sram = {}
            self.PC = 0
            self.SP = 0x085F
            self.SREG = 0
            self._update_reg_table()
            self._update_mem_grid()
            self._update_pc_sp_sreg()
            self.log_view.append("<font color='blue'>[GUI] All memory cleared.</font>")

    def _poll_queues(self):
        # Process Stdout/Stderr
        for _ in range(50):
            try:
                msg_type, text = self.emulator_queue.get_nowait()
                if msg_type == "stdout":
                    self.log_view.append(f"<font color='gray'>[Out] {text}</font>")
                    
                    # NEW: Watch for the LED Status string explicitly
                    if "[Port B] ---> [ LED STATUS:" in text:
                        if "ON" in text:
                            self.led_indicator.setStyleSheet("background-color: #00FF00; border-radius: 10px; border: 2px solid #005500;") # Green
                        elif "OFF" in text:
                            self.led_indicator.setStyleSheet("background-color: #550000; border-radius: 10px; border: 2px solid #220000;") # Red

                    # Attempt to parse JSON if line looks like it
                    if "{" in text and "}" in text:
                        try:
                            json_str = text[text.find("{"):text.rfind("}")+1]
                            obj = json.loads(json_str)
                            self.event_pump.json_received.emit(obj)
                        except: pass
                elif msg_type == "stderr":
                    self.log_view.append(f"<font color='red'>[Err] {text}</font>")
            except Empty: break

        # Process Socket JSON
        for _ in range(100):
            try:
                obj = self.socket_queue.get_nowait()
                if "__meta__" in obj:
                    meta = obj.get("__meta__")
                    if meta != "connected":
                        self.log_view.append(f"<font color='orange'>[Socket] {meta}</font>")
                    continue
                self.event_pump.json_received.emit(obj)
            except Empty: break

    def handle_json_event(self, obj):
        t = obj.get("type")
        
        if t == "reg":
            r, val = obj.get("r"), obj.get("value")
            if isinstance(r, int) and 0 <= r < 32:
                self.regs[r] = val & 0xFF
                self._update_reg_table()
        
        elif t == "mem" and obj.get("space") == "sram":
            addr, val = obj.get("addr"), obj.get("value")
            if addr is not None:
                self.sram[addr] = val & 0xFF
                self._update_mem_grid()
                # LED memory-watching logic removed as it is now handled by stdout parsing
        
        elif t in ["cpu_state", "instruction", "instr_fetch"]:
            if "pc" in obj: self.PC = obj["pc"]
            if "new_pc" in obj: self.PC = obj["new_pc"]
            if "sp" in obj: 
                try: self.SP = int(str(obj["sp"]), 16) if 'x' in str(obj["sp"]) else int(obj["sp"])
                except: pass
            if "sreg" in obj: 
                try: self.SREG = int(str(obj["sreg"]), 16) if 'x' in str(obj["sreg"]) else int(obj["sreg"])
                except: pass
            self._update_pc_sp_sreg()                           

    def _update_reg_table(self):
        self.reg_table.setUpdatesEnabled(False)
        for row in range(16):
            for col, offset in [(0, 0), (1, 16)]:
                idx = row + offset
                item = QTableWidgetItem(f"R{idx:02}: 0x{self.regs[idx]:02X}")
                item.setFlags(Qt.ItemIsEnabled)
                if self.regs[idx] != 0: item.setForeground(Qt.blue)
                self.reg_table.setItem(row, col, item)
        self.reg_table.setUpdatesEnabled(True)

    def _update_pc_sp_sreg(self):
        self.pc_lbl.setText(f"0x{self.PC:04X}")
        self.sp_lbl.setText(f"0x{self.SP:04X}")
        self.sreg_lbl.setText(f"0x{self.SREG:02X}")

    def _update_mem_grid(self):
        self.mem_grid.setUpdatesEnabled(False)
        self.mem_grid.setColumnCount(5)
        self.mem_grid.setRowCount(4)
        idx = 0
        for r in range(4):
            for c in range(5):
                addr = self.mem_page_base + idx
                val = self.sram.get(addr, None)
                txt = f"{addr:04X}: {val:02X}" if val is not None else f"{addr:04X}: --"
                item = QTableWidgetItem(txt)
                item.setFlags(Qt.ItemIsEnabled)
                self.mem_grid.setItem(r, c, item)
                idx += 1
        self.mem_grid.setUpdatesEnabled(True)

    def set_page_base(self):
        try: self.mem_page_base = int(self.page_base_edit.text(), 16); self._update_mem_grid()
        except: pass

    def page_prev(self):
        self.mem_page_base = max(0, self.mem_page_base - 20)
        self.page_base_edit.setText(f"{self.mem_page_base:04X}"); self._update_mem_grid()

    def page_next(self):
        self.mem_page_base += 20
        self.page_base_edit.setText(f"{self.mem_page_base:04X}"); self._update_mem_grid()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    w = MainWindow()
    w.show()
    sys.exit(app.exec_())