import argparse
from collections import deque
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import uuid
import webbrowser

import serial
from serial.tools import list_ports

from diagnostic_model import APP_VERSION, DiagnosticRun, TEST_PROFILE, diagnostic_steps, validate_state


ROOT = Path(__file__).resolve().parents[2]
REPORTS = ROOT / 'diagnostics' / 'reports'
PROFILE = ROOT / 'diagnostics' / 'hardware_profile.json'
HEX = ROOT / 'diagnostics/teensy/amen_diagnostic/build/amen_diagnostic.ino.hex'
VERDICTS = {
    'controles_reussis_profil_a_qualifier': 'Contrôles réussis — profil à qualifier sur matériel ; expédition non validée',
    'non_conforme': 'Non conforme aux contrôles effectués',
    'incomplet': 'Incomplet — contrôles obligatoires manquants ou interrompus',
    'conforme_aux_controles_effectues': 'Conforme aux contrôles effectués',
}
PATTERN_NAMES = {
    'black': 'Noir intégral', 'white': 'Blanc intégral', 'checker': 'Damier 4 × 4',
    'inverse': 'Damier inversé', 'border': 'Bordure', 'rows': 'Lignes', 'columns': 'Colonnes',
}


def find_loader():
    repository_loader = ROOT / 'teensy.exe'
    if repository_loader.is_file():
        return str(repository_loader)
    roots = [Path(os.environ.get('LOCALAPPDATA', str(Path.home()))) / 'Arduino15',
             Path(tempfile.gettempdir()) / 'opencode' / 'amen-arduino-data']
    for root in roots:
        candidate = root / 'packages/teensy/tools/teensy-tools/1.62.0/teensy.exe'
        if candidate.is_file():
            return str(candidate)
    return ''


def make_step_labels(profile):
    labels = []
    for number, step in enumerate(diagnostic_steps(profile), 1):
        if step['kind'] == 'idle':
            name = 'Repos initial'
        elif step['kind'] == 'encoder':
            name = f'Rotation {profile["encoders"][step["encoder"]]["label"]}'
        elif step['kind'] == 'oled':
            name = f'OLED — {PATTERN_NAMES[step["pattern"]]}'
        elif len(step['contacts']) == 1:
            name = f'Contact {profile["contacts"][step["contacts"][0]]["label"]}'
        else:
            matrix_index = int(step['id'].split('_')[1]) - 1
            descriptions = profile.get('combination_labels', [])
            name = f'Matrice — {descriptions[matrix_index]}' if matrix_index < len(descriptions) else f'Combinaison matrice {matrix_index + 1}'
        labels.append(f'{number:02d} — {name}')
    return labels


class DiagnosticApp:
    def __init__(self, root, profile):
        self.root = root
        self.profile = profile
        self.port = None
        self.hello = None
        self.pending = {}
        self.command_id = 0
        self.buffer = bytearray()
        self.last_state_at = None
        self.last_seq = None
        self.last_ms = None
        self.state = None
        self.run = None
        self.verdict_run = None
        self.verdict_label = ''
        self.trace = deque(maxlen=2000)
        self.trace_truncated = 0
        self.pattern = None
        self.pattern_ack = False
        self.pattern_fresh = False
        self.free = False
        self.saved = False
        self.closed = False
        self.retrying = False
        self.start_index = 0
        self.step_labels = make_step_labels(profile)
        self.root.title(f'AMEN MINI — Diagnostic d’assemblage {APP_VERSION}')
        self.root.geometry('1180x900')
        self.root.minsize(900, 680)
        self.root.protocol('WM_DELETE_WINDOW', self.close)
        self.status = tk.StringVar(value='Déconnecté — aucun diagnostic matériel effectué')
        self.instruction = tk.StringVar(value='Préparer la première alimentation, puis choisir le port du diagnostic.')
        self.report_status = tk.StringVar(value='Aucun rapport enregistré')
        self.build_ui()
        self.refresh_ports()
        self.root.after(30, self.tick)

    def build_ui(self):
        shell = ttk.Frame(self.root, padding=10)
        shell.pack(fill='both', expand=True)
        ttk.Label(shell, textvariable=self.status).pack(anchor='w')
        self.preparation_visible = tk.BooleanVar(value=True)
        ttk.Checkbutton(shell, text='Afficher l’aide de démarrage', variable=self.preparation_visible, command=self.toggle_preparation).pack(anchor='w')
        self.preparation_panel = ttk.Frame(shell)
        self.preparation_panel.pack(fill='x', pady=4)
        setup_canvas = tk.Canvas(self.preparation_panel, height=300, highlightthickness=0)
        setup_scroll = ttk.Scrollbar(self.preparation_panel, orient='vertical', command=setup_canvas.yview)
        setup_scroll.pack(side='right', fill='y')
        setup_canvas.pack(side='left', fill='x', expand=True)
        setup_canvas.configure(yscrollcommand=setup_scroll.set)
        tabs = ttk.Notebook(setup_canvas)
        setup_window = setup_canvas.create_window(0, 0, window=tabs, anchor='nw')
        tabs.bind('<Configure>', lambda event: setup_canvas.configure(scrollregion=setup_canvas.bbox('all')))
        setup_canvas.bind('<Configure>', lambda event: setup_canvas.itemconfigure(setup_window, width=event.width))
        preparation = ttk.Frame(tabs, padding=8)
        installation = ttk.Frame(tabs, padding=8)
        tabs.add(preparation, text='Avant de brancher')
        tabs.add(installation, text='Installer le diagnostic sur une Teensy vierge')
        self.build_preflight(preparation)
        self.build_install(installation)
        connection = ttk.Frame(shell)
        connection.pack(fill='x', pady=5)
        self.connection_panel = connection
        self.port_name = tk.StringVar()
        self.ports = ttk.Combobox(connection, textvariable=self.port_name, state='readonly', width=18)
        self.ports.pack(side='left')
        ttk.Button(connection, text='Actualiser les ports', command=self.refresh_ports).pack(side='left')
        ttk.Button(connection, text='Connecter', command=self.connect).pack(side='left')
        ttk.Button(connection, text='Déconnecter', command=lambda: self.disconnect('Déconnexion opérateur')).pack(side='left')
        actions = ttk.Frame(shell)
        actions.pack(fill='x')
        ttk.Button(actions, text='Nouveau parcours / réessayer', command=self.new_run).pack(side='left')
        ttk.Label(actions, text='Commencer à :').pack(side='left', padx=(10, 3))
        self.start_step = tk.StringVar(value=self.step_labels[0])
        self.start_picker = ttk.Combobox(actions, textvariable=self.start_step, values=self.step_labels, width=43, state='readonly')
        self.start_picker.pack(side='left')
        self.retry_button = ttk.Button(actions, text='Erreur de manipulation — reprendre ce test', command=self.retry_current, state='disabled')
        self.retry_button.pack(side='left', padx=(6, 0))
        ttk.Button(actions, text='Vue libre', command=self.free_view).pack(side='left')
        ttk.Button(actions, text='Exporter JSON', command=self.export).pack(side='right')
        instruction_label = ttk.Label(shell, textvariable=self.instruction, font=('Segoe UI', 13, 'bold'), wraplength=850)
        instruction_label.pack(fill='x', pady=6)
        instruction_label.bind('<Configure>', lambda event: instruction_label.configure(wraplength=event.width))
        self.progress = ttk.Progressbar(shell, maximum=1)
        self.progress.pack(fill='x')
        self.progress_text = ttk.Label(shell, text='0 / 0 — Non vérifié')
        self.progress_text.pack(anchor='w')
        self.canvas = tk.Canvas(shell, background='#18232b', highlightthickness=0, height=280)
        self.canvas.pack(fill='both', expand=True, pady=6)
        self.canvas.bind('<Configure>', lambda event: self.draw_board())
        self.board_legend = ttk.Label(shell, text='○ À tester   ● En cours   ✓ Réussi   × Échec   ? Non vérifié   ↓ Appuyé  |  Appui = poussoir ; Δ = rotation', wraplength=850)
        self.board_legend.pack(anchor='w')
        visual = ttk.Frame(shell)
        visual.pack(fill='x')
        self.preview = tk.Canvas(visual, width=256, height=64, background='black', highlightthickness=1)
        self.preview.pack(side='left', padx=(0, 10))
        visual_controls = ttk.Frame(visual)
        visual_controls.pack(side='left', fill='x', expand=True)
        self.visual_pass = ttk.Button(visual_controls, text='OLED correct (manuel)', command=lambda: self.confirm_visual(True), state='disabled')
        self.visual_pass.grid(row=0, column=0, sticky='w')
        self.visual_fail = ttk.Button(visual_controls, text='Défaut OLED (manuel)', command=lambda: self.confirm_visual(False), state='disabled')
        self.visual_fail.grid(row=0, column=1, sticky='w')
        self.replay = ttk.Button(visual_controls, text='Rejouer', command=self.replay_pattern, state='disabled')
        self.replay.grid(row=0, column=2, sticky='w')
        self.free_pattern_name = tk.StringVar(value=PATTERN_NAMES['black'])
        ttk.Label(visual_controls, text='Mire en vue libre :').grid(row=1, column=0, sticky='w')
        self.free_patterns = ttk.Combobox(visual_controls, textvariable=self.free_pattern_name, values=list(PATTERN_NAMES.values()), width=20, state='disabled')
        self.free_patterns.grid(row=1, column=1, columnspan=2, sticky='w')
        self.free_patterns.bind('<<ComboboxSelected>>', self.select_free_pattern)
        report_label = ttk.Label(shell, textvariable=self.report_status, wraplength=850)
        report_label.pack(anchor='w', pady=4)
        report_label.bind('<Configure>', lambda event: report_label.configure(wraplength=event.width))
        self.technical = tk.BooleanVar()
        ttk.Checkbutton(shell, text='Détails techniques : profil, états bruts et journal', variable=self.technical, command=self.toggle_technical).pack(anchor='w')
        self.log = tk.Text(shell, height=4, wrap='word', state='disabled')
        self.log_line(json.dumps(self.profile, ensure_ascii=False))
        self.toggle_preparation()

    def toggle_preparation(self):
        if self.preparation_visible.get():
            self.preparation_panel.pack(fill='x', pady=4, before=self.connection_panel)
            self.canvas.pack_forget()
        else:
            self.preparation_panel.pack_forget()
            self.canvas.pack(fill='both', expand=True, pady=6, before=self.board_legend)

    def build_preflight(self, frame):
        text = ('1. Carte hors tension : inspecter rapidement les soudures, les composants et l’orientation des modules.\n'
                '2. Au multimètre : vérifier 3V3 / GND, VIN / GND et 3V3 / VIN.\n'
                '3. Si les mesures paraissent normales, brancher la Teensy en USB.\n'
                '4. Ne pas alimenter séparément VIN pendant que l’USB alimente la carte.')
        ttk.Label(frame, text=text, wraplength=800, justify='left').pack(anchor='w')

    def build_install(self, frame):
        text = ('Une Teensy vierge peut ne présenter aucun port COM : c’est normal avant chargement du diagnostic.\n'
                'Télécharger Teensy Loader depuis PJRC, sélectionner son exécutable et le .hex diagnostic Teensy 4.1.\n'
                'Ouvrir le chargeur, suivre ses instructions et utiliser le bouton Program de la Teensy si demandé.\n'
                'Le lancement ci-dessous ouvre un outil externe ; il ne confirme pas une programmation réussie.\n'
                'Après chargement, actualiser les ports et sélectionner explicitement celui de la Teensy.')
        ttk.Label(frame, text=text, wraplength=800).pack(anchor='w')
        ttk.Button(frame, text='Ouvrir la page officielle Teensy Loader', command=lambda: webbrowser.open('https://www.pjrc.com/teensy/loader.html')).pack(anchor='w', pady=4)
        self.loader_path = tk.StringVar(value=find_loader())
        self.hex_path = tk.StringVar(value=str(HEX))
        for label, variable, types in [('Chargeur', self.loader_path, [('Exécutable', '*.exe')]), ('Binaire diagnostic', self.hex_path, [('Intel HEX', '*.hex')])]:
            row = ttk.Frame(frame)
            row.pack(fill='x')
            ttk.Label(row, text=label, width=20).pack(side='left')
            ttk.Entry(row, textvariable=variable).pack(side='left', fill='x', expand=True)
            ttk.Button(row, text='Parcourir', command=lambda v=variable, t=types: self.browse(v, t)).pack(side='left')
        ttk.Button(frame, text='Ouvrir le chargeur avec ce .hex', command=self.launch_loader).pack(anchor='w', pady=4)

    def browse(self, variable, types):
        path = filedialog.askopenfilename(filetypes=types)
        if path:
            variable.set(path)

    def launch_loader(self):
        loader, binary = Path(self.loader_path.get()), Path(self.hex_path.get())
        if not loader.is_file() or not binary.is_file() or binary.suffix.lower() != '.hex':
            messagebox.showerror('Installation', 'Choisir un exécutable existant et un fichier .hex compilé. Le binaire peut nécessiter une compilation préalable.')
            return
        if self.port:
            self.disconnect('Ouverture du chargeur externe')
        try:
            subprocess.Popen([str(loader.resolve()), str(binary.resolve())], shell=False)
            self.status.set('Chargeur ouvert — chargement à effectuer et vérifier dans Teensy Loader')
        except OSError as error:
            messagebox.showerror('Chargeur inaccessible', str(error))

    def refresh_ports(self):
        try:
            self.ports['values'] = [port.device for port in list_ports.comports()]
        except (OSError, serial.SerialException) as error:
            self.status.set(f'Énumération des ports impossible : {error}')

    def connect(self):
        if self.port or not self.port_name.get():
            self.status.set('Choisir un port explicitement ; déconnecter la liaison précédente si nécessaire.')
            return
        try:
            self.port = serial.Serial(self.port_name.get(), 115200, timeout=0, write_timeout=0.05)
            self.port.reset_input_buffer()
            self.buffer.clear()
            self.hello = None
            self.last_state_at = None
            self.last_seq = self.last_ms = None
            self.status.set('Port ouvert — vérification de la signature diagnostic…')
            self.send('hello')
        except (OSError, serial.SerialException) as error:
            self.disconnect(f'Port inaccessible : {error}. Aucune conclusion sur la carte.')

    def send(self, command, **fields):
        if not self.port:
            return
        self.command_id += 1
        identifier = self.command_id
        packet = {'cmd': command, 'id': identifier, **fields}
        self.pending[identifier] = (command, time.monotonic() + 2, fields)
        try:
            data = (json.dumps(packet) + '\n').encode('ascii')
            if self.port.write(data) != len(data):
                raise serial.SerialTimeoutException('Écriture partielle')
            self.log_line('TX ' + data.decode().strip())
        except (OSError, serial.SerialException) as error:
            self.disconnect(f'Écriture série interrompue : {error}')

    def disconnect(self, reason):
        if self.run:
            self.run.interrupt(reason)
            self.saved = False
        port, self.port = self.port, None
        if port:
            try:
                port.close()
            except (OSError, serial.SerialException) as error:
                reason += f' ; fermeture : {error}'
        self.pending.clear()
        self.buffer.clear()
        self.hello = None
        self.state = None
        self.verdict_run = None
        self.verdict_label = ''
        self.pattern = None
        self.pattern_ack = False
        self.pattern_fresh = False
        self.retrying = False
        self.draw_pattern()
        self.last_state_at = None
        self.status.set(reason)
        self.log_line(reason)
        self.refresh_view()

    def preserve_run(self, reason):
        if not self.run:
            return True
        if not self.run.finished:
            self.run.interrupt(reason)
            self.saved = False
        if not self.save_report():
            return False
        self.run = None
        self.verdict_run = None
        self.trace = deque(maxlen=2000)
        self.trace_truncated = 0
        self.pattern = None
        self.pattern_ack = False
        return True

    def new_run(self):
        if not self.hello or not self.port:
            messagebox.showinfo('Connexion requise', 'Connecter un firmware diagnostic compatible avant le parcours.')
            return
        if self.pending:
            return
        if not self.preserve_run('Nouvelle tentative demandée'):
            return
        try:
            self.start_index = self.step_labels.index(self.start_step.get())
        except ValueError:
            self.start_index = 0
            self.start_step.set(self.step_labels[0])
        self.start_metadata = {}
        self.free = False
        self.state = None
        self.last_seq = self.last_ms = None
        try:
            self.port.reset_input_buffer()
            self.buffer.clear()
            self.send('start')
        except (OSError, serial.SerialException) as error:
            self.disconnect(f'Début de parcours impossible : {error}')
            return
        self.instruction.set('Attente de l’accusé de début — aucune mesure antérieure ne compte.')

    def retry_current(self):
        if (not self.port or not self.hello or self.pending or not self.run
                or (self.run.finished and not self.run.failed)):
            return
        try:
            self.port.reset_input_buffer()
            self.buffer.clear()
            self.retrying = True
            self.send('start')
        except (OSError, serial.SerialException) as error:
            self.disconnect(f'Reprise impossible : {error}')
            return
        self.instruction.set('Remise à zéro des mesures — reprise du test en échec…')

    def free_view(self):
        if self.pending:
            return
        if not self.preserve_run('Passage en observation libre'):
            return
        self.free = True
        self.verdict_run = None
        self.verdict_label = ''
        self.pattern = None
        self.pattern_ack = False
        self.pattern_fresh = False
        self.draw_pattern()
        if not self.port:
            self.state = None
        self.refresh_view()
        if self.hello:
            self.send('snapshot')

    def receive(self, packet):
        if not isinstance(packet, dict):
            raise ValueError('Objet JSON attendu')
        kind = packet.get('type')
        if kind in ('hello', 'ack'):
            identifier = packet.get('id')
            if type(identifier) is not int or identifier not in self.pending:
                raise ValueError('Réponse sans commande corrélée')
            command, deadline, fields = self.pending.pop(identifier)
            if time.monotonic() > deadline:
                raise ValueError('Accusé reçu après échéance')
            if command == 'hello':
                if kind != 'hello' or type(packet.get('protocol')) is not int or packet['protocol'] != 1 or packet.get('profile') != self.profile['id'] or not packet.get('firmware') or 'boot' not in packet:
                    raise ValueError('Signature, protocole ou profil incompatible')
                self.hello = packet
                self.last_state_at = time.monotonic()
                self.preparation_visible.set(False)
                self.toggle_preparation()
                self.status.set(f'Diagnostic connecté — firmware {packet["firmware"]} — profil {packet["profile"]}')
            else:
                if kind != 'ack' or type(packet.get('ok')) is not bool:
                    raise ValueError('Accusé mal formé')
                if command == 'pattern':
                    self.pattern_ack = packet['ok']
                    self.pattern_fresh = False
                    if self.run:
                        self.run.pattern_ready(packet['ok'])
                    elif self.free:
                        self.status.set('Mire transmise — observation libre, confirmation visuelle nécessaire' if packet['ok']
                                        else 'Mire non transmise : vérifier l’OLED et ses connexions. Les contacts restent observables.')
                if not packet['ok'] and command != 'pattern':
                    raise ValueError(f'Commande {command} refusée')
                if command == 'start':
                    if self.retrying:
                        self.run.retry_current()
                        self.retrying = False
                    else:
                        self.run = DiagnosticRun(self.profile, self.start_metadata, dict(self.hello), self.start_index)
                        self.trace = deque(maxlen=2000)
                        self.trace_truncated = 0
                    self.saved = False
                    self.last_seq = self.last_ms = None
                    self.last_state_at = time.monotonic()
            self.log_line('RX ' + json.dumps(packet, ensure_ascii=False))
        elif kind == 'state':
            validate_state(packet)
            if not self.hello or any(value[0] == 'start' for value in self.pending.values()):
                return
            stream_error = None
            if self.last_seq is not None:
                gap = (packet['ms'] - self.last_ms) & 0xFFFFFFFF
                if packet['seq'] != (self.last_seq + 1) & 0xFFFFFFFF or gap > TEST_PROFILE['max_frame_gap_ms']:
                    stream_error = 'Perte de séquence, intervalle excessif ou redémarrage détecté'
            if packet['overflow']:
                stream_error = 'Acquisition incomplète signalée par le firmware — nouveau parcours requis pour remettre les compteurs à zéro'
            self.last_seq, self.last_ms = packet['seq'], packet['ms']
            self.last_state_at = time.monotonic()
            self.state = packet
            if self.run and not self.run.finished:
                if len(self.trace) == self.trace.maxlen:
                    self.trace_truncated += 1
                self.trace.append({'pc_monotonic': self.last_state_at, 'state': packet})
                self.run.update(packet)
                if self.pattern_ack:
                    self.pattern_fresh = True
                self.saved = False
            if stream_error:
                self.status.set(stream_error)
        else:
            raise ValueError('Type de message inconnu')

    def tick(self):
        if self.closed:
            return
        try:
            if self.port:
                now = time.monotonic()
                if any(now > value[1] for value in self.pending.values()):
                    raise ValueError('Délai de réponse dépassé ; aucune conclusion sur la cause matérielle')
                if self.last_state_at is not None and now - self.last_state_at > 2:
                    raise ValueError('Aucun état frais depuis 2 secondes')
                self.buffer.extend(self.port.read(min(self.port.in_waiting, 32768)))
                if len(self.buffer) > 131072:
                    raise ValueError('Tampon série saturé')
                for _ in range(64):
                    end = self.buffer.find(b'\n')
                    if end < 0:
                        break
                    if end > 16384:
                        raise ValueError('Trame trop longue')
                    line = bytes(self.buffer[:end])
                    del self.buffer[:end + 1]
                    self.receive(json.loads(line.decode('utf-8')))
                if len(self.buffer) > 16384 and b'\n' not in self.buffer:
                    raise ValueError('Trame incomplète trop longue')
            self.refresh_view()
        except (OSError, serial.SerialException, ValueError, KeyError, TypeError) as error:
            self.disconnect(f'Mesures interrompues : {error}')
        self.root.after(30, self.tick)

    def refresh_view(self):
        if self.run:
            self.instruction.set(self.run.instruction)
            done, total = self.run.progress
            self.progress.configure(maximum=max(total, 1), value=done)
            label = 'Échec' if self.run.failed else ('Terminé — consulter le rapport' if self.run.finished else 'En cours')
            if self.run.finished:
                if self.verdict_run is not self.run:
                    verdict = self.run.report()['verdict']
                    self.verdict_label = VERDICTS.get(verdict, verdict)
                    self.verdict_run = self.run
                label = self.verdict_label
            self.progress_text.configure(text=f'{done} / {total} — {label} — qualification des seuils requise avant expédition')
            pattern = self.run.current_pattern
            if pattern != self.pattern:
                self.pattern = pattern
                self.pattern_ack = False
                self.pattern_fresh = False
                self.draw_pattern()
                if pattern and self.port and not self.run.finished:
                    self.send('pattern', pattern=pattern)
        elif self.free:
            self.instruction.set('Vue libre — observer les contacts et rotations ; aucun verdict de conformité.')
            self.progress.configure(value=0)
            self.progress_text.configure(text='Non vérifié — observation libre')
        enabled = bool(self.run and not self.run.finished and self.pattern and self.pattern_ack and self.pattern_fresh and self.run.pattern_seen and self.port)
        for button in (self.visual_pass, self.visual_fail):
            button.configure(state='normal' if enabled else 'disabled')
        free_ready = bool(self.free and not self.run and self.port and self.hello and not self.pending)
        self.free_patterns.configure(state='readonly' if free_ready else 'disabled')
        self.replay.configure(state='normal' if (enabled or (free_ready and self.pattern_ack and self.pattern)) and not self.pending else 'disabled')
        retry_ready = bool(self.run and (not self.run.finished or self.run.failed)
                           and self.port and self.hello and not self.pending)
        self.retry_button.configure(state='normal' if retry_ready else 'disabled')
        picker_ready = not self.run or self.run.finished
        self.start_picker.configure(state='readonly' if picker_ready else 'disabled')
        self.draw_board()

    def confirm_visual(self, passed):
        if self.run and self.pattern_ack and self.pattern_fresh and self.run.pattern_seen and self.port and not self.run.finished:
            self.run.confirm_visual(passed)
            self.pattern_ack = False
            self.saved = False
            self.refresh_view()

    def replay_pattern(self):
        if self.pattern and (self.run or self.free) and self.hello and not self.pending:
            self.pattern_ack = False
            self.pattern_fresh = False
            self.send('pattern', pattern=self.pattern)
            self.refresh_view()

    def select_free_pattern(self, event=None):
        if not self.free or self.run or not self.hello or self.pending:
            return
        self.pattern = next(key for key, label in PATTERN_NAMES.items() if label == self.free_pattern_name.get())
        self.pattern_ack = False
        self.pattern_fresh = False
        self.draw_pattern()
        self.send('pattern', pattern=self.pattern)
        self.refresh_view()

    def draw_pattern(self):
        self.preview.delete('all')
        pattern = str(self.pattern or '').lower()
        for y in range(32):
            for x in range(128):
                on = False
                if pattern == 'white':
                    on = True
                elif pattern in ('checker', 'inverse'):
                    on = ((x // 4 + y // 4) & 1) == (0 if pattern == 'checker' else 1)
                elif pattern == 'border':
                    on = x in (0, 127) or y in (0, 31)
                elif pattern == 'rows':
                    on = y % 2 == 0
                elif pattern == 'columns':
                    on = x % 2 == 0
                if on:
                    self.preview.create_rectangle(x * 2, y * 2, x * 2 + 2, y * 2 + 2, fill='white', outline='')

    def draw_board(self):
        self.canvas.delete('all')
        contacts, encoders = self.profile['contacts'], self.profile['encoders']
        items = contacts + encoders
        xs, ys = [item['x'] for item in items], [item['y'] for item in items]
        width, height = max(self.canvas.winfo_width(), 100), max(self.canvas.winfo_height(), 80)
        scale = min((width - 100) / max(max(xs) - min(xs), 1), (height - 80) / max(max(ys) - min(ys), 1))
        targets = self.run.target_contacts if self.run and not self.run.finished else set()
        pushes = {item['contact_index'] for item in encoders}
        for is_encoder, group in ((False, contacts), (True, encoders)):
            for index, item in enumerate(group):
                if not is_encoder and index in pushes:
                    continue
                x = (item['x'] - (min(xs) + max(xs)) / 2) * scale + width / 2
                y = (item['y'] - (min(ys) + max(ys)) / 2) * scale + height / 2
                if is_encoder:
                    active = self.run and not self.run.finished and self.run.target_encoder == index
                    result = self.run.results.get(f'rotation_{item["id"]}', {}).get('status') if self.run else None
                    symbol, color = self.board_status(result, active)
                    radius = max(20, min(24, scale * 6))
                    self.canvas.create_oval(x - radius, y - radius, x + radius, y + radius, outline=color, width=2)
                    count = self.state['encoders'][index] if self.state else '—'
                    self.canvas.create_text(x, y - radius - 9, text=f'Δ {count} {symbol}', fill=color, font=('Segoe UI', 8))
                    self.canvas.create_text(x, y - 6, text=item['id'], fill=color, font=('Segoe UI', 8, 'bold'))
                    contact_index = item['contact_index']
                    push_result = self.run.results.get(contacts[contact_index]['id'], {}).get('status') if self.run else None
                    pressed = bool(self.state and self.state['contacts'][contact_index])
                    push_symbol, push_color = self.board_status(push_result, contact_index in targets, pressed)
                    self.canvas.create_text(x, y + 8, text=f'Appui {push_symbol}', fill=push_color, font=('Segoe UI', 7))
                else:
                    pressed = bool(self.state and self.state['contacts'][index])
                    result = self.run.results.get(item['id'], {}).get('status') if self.run else None
                    symbol, color = self.board_status(result, index in targets, pressed)
                    half_width, half_height = min(26, max(17, scale * 7)), min(16, max(12, scale * 4.5))
                    self.canvas.create_rectangle(x - half_width, y - half_height, x + half_width, y + half_height, outline=color, width=2)
                    self.canvas.create_text(x, y - 5, text=item['id'], fill=color, font=('Segoe UI', 8, 'bold'))
                    self.canvas.create_text(x, y + 8, text=symbol, fill=color, font=('Segoe UI', 8))
        if self.technical.get() and self.state:
            self.log_line('ÉTAT ' + json.dumps(self.state, ensure_ascii=False), state=True)

    def board_status(self, result, target=False, pressed=False):
        if result == 'failed':
            return '×', '#f17f7f'
        if pressed:
            return '↓', '#62c9b5'
        if target:
            return '●', '#e7b44d'
        return {
            'passed': ('✓', '#62c9b5'), 'interrupted': ('?', '#c2c9d0'),
            'running': ('●', '#e7b44d'), 'not_tested': ('○', '#c2c9d0'),
        }.get(result, ('?', '#c2c9d0'))

    def toggle_technical(self):
        if self.technical.get():
            self.log.pack(fill='x')
        else:
            self.log.pack_forget()

    def log_line(self, text, state=False):
        if state and getattr(self, 'logged_seq', None) == self.state['seq']:
            return
        if state:
            self.logged_seq = self.state['seq']
        self.log.configure(state='normal')
        self.log.insert('end', text + '\n')
        if int(self.log.index('end-1c').split('.')[0]) > 300:
            self.log.delete('1.0', '100.0')
        self.log.see('end')
        self.log.configure(state='disabled')

    def save_report(self, path=None):
        if not self.run:
            self.report_status.set('Aucun parcours à exporter ; la vue libre ne valide pas une carte.')
            return False
        try:
            if path is None:
                REPORTS.mkdir(parents=True, exist_ok=True)
                path = REPORTS / f'diagnostic_{time.strftime("%Y%m%d_%H%M%S")}_{uuid.uuid4().hex[:12]}.json'
            report = self.run.report()
            report['pc_trace'] = list(self.trace)
            report['pc_trace_truncated'] = self.trace_truncated
            with Path(path).open('x', encoding='utf-8') as output:
                json.dump(report, output, ensure_ascii=False, indent=2)
                output.write('\n')
                output.flush()
                os.fsync(output.fileno())
            self.saved = True
            verdict = report.get('verdict', 'voir rapport')
            self.report_status.set(f'Rapport enregistré : {path} — {VERDICTS.get(verdict, verdict)}')
            return True
        except (OSError, TypeError, ValueError) as error:
            self.report_status.set(f'ÉCHEC d’enregistrement : {error}. Le parcours reste en mémoire.')
            messagebox.showerror('Rapport non enregistré', str(error))
            return False

    def export(self):
        if not self.run:
            self.save_report()
            return
        path = filedialog.asksaveasfilename(title='Nouveau fichier JSON (aucun écrasement)', defaultextension='.json', filetypes=[('Rapport JSON', '*.json')], initialfile=f'diagnostic_{uuid.uuid4().hex[:8]}.json')
        if path:
            self.save_report(path)

    def close(self):
        if self.run and (not self.saved or not self.run.finished):
            if not self.run.finished:
                self.run.interrupt('Fermeture de l’application')
            if not self.save_report():
                return
        if self.port:
            try:
                self.port.close()
            except (OSError, serial.SerialException) as error:
                messagebox.showerror('Fermeture du port', str(error))
        self.closed = True
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--smoke-test', action='store_true')
    parser.add_argument('--profile', type=Path, default=PROFILE)
    args = parser.parse_args()
    with args.profile.open(encoding='utf-8') as source:
        profile = json.load(source)
    if not profile.get('id') or len(profile['contacts']) != 28 or len(profile['encoders']) != 7:
        raise ValueError('Profil matériel incomplet')
    root = tk.Tk()
    app = DiagnosticApp(root, profile)
    if args.smoke_test:
        root.update()
        app.draw_board()
        app.close()
        print('Tkinter : fenêtre créée et rafraîchie, aucun port ouvert, aucun test matériel effectué.')
    else:
        root.mainloop()


if __name__ == '__main__':
    main()
