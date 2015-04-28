import sys
import re
import zmq
import time
import numpy as np

from PyQt4 import Qt
import pyqtgraph as pg
from pyqtgraph.Qt import QtCore, QtGui
from pyqtgraph.dockarea import DockArea, Dock
import pyqtgraph.ptime as ptime

from pybar.analysis.RawDataConverter.data_interpreter import PyDataInterpreter
from pybar.analysis.RawDataConverter.data_histograming import PyDataHistograming


class DataWorker(QtCore.QObject):
    run_start = QtCore.pyqtSignal()
    config_changed = QtCore.pyqtSignal(dict)
    data_ready = QtCore.pyqtSignal(tuple, list)

    def __init__(self):
        QtCore.QObject.__init__(self)
        self.integrate_readouts = 1
        self.n_readout = 0
        self.n_hits = 0  # integrated hits
        self.setup_raw_data_analysis()

    def setup_raw_data_analysis(self):
        self.interpreter = PyDataInterpreter()
        self.histograming = PyDataHistograming()
        self.interpreter.set_warning_output(False)
        self.histograming.set_no_scan_parameter()
        self.histograming.create_occupancy_hist(True)
        self.histograming.create_rel_bcid_hist(True)
        self.histograming.create_tot_hist(True)
        self.histograming.create_tdc_hist(True)

    def connect(self, socket_addr):
        self.socket_addr = socket_addr
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.PULL)
        self.socket.connect(self.socket_addr)
        self.poller = zmq.Poller()  # poll needed to be able to return QThread
        self.poller.register(self.socket, zmq.POLLIN)

    def set_integrate_readouts(self, value):
        self.integrate_readouts = value

    def reset_data(self):
        self.histograming.reset()
        self.interpreter.reset()

    def recv_data(self, flags=0, copy=True, track=False):
        if self.poller.poll(10):
            meta_data = self.socket.recv_json(flags=flags)
            if meta_data['name'] == 'FEI4readoutData':  # check that the data is FE-I4 readout data (raw data + readout meta data)
                msg = self.socket.recv(flags=flags, copy=copy, track=track)
                array = np.fromstring(msg, dtype=meta_data['dtype'])
                return array.reshape(meta_data['shape']), meta_data['timestamp_start'], meta_data['timestamp_stop'], meta_data['readout_error'], re.sub(r'\bOrderedDict\b|[()\[\],\']', '', meta_data['scan_parameters'])
            elif 'Conf' in meta_data['name']:  # check that the data is the run conf
                return meta_data['name'], meta_data['conf']

    def analyze_raw_data(self, raw_data):
        self.interpreter.interpret_raw_data(raw_data)
        self.histograming.add_hits(self.interpreter.get_hits())

    def process_data(self):  # main loop
        data = self.recv_data()  # poll on ZMQ queue for data
        if data and len(data) == 5:  # identify non empty raw data by existing time stamp
            self.n_readout += 1
            if self.integrate_readouts != 0 and self.n_readout % self.integrate_readouts == 0:
                self.n_hits = np.sum(self.histograming.get_occupancy())
                self.histograming.reset()
            self.analyze_raw_data(data[0])
            interpreted_data = self.histograming.get_occupancy(), self.histograming.get_tot_hist(), self.interpreter.get_tdc_counters(), self.interpreter.get_error_counters(), self.interpreter.get_service_records_counters(), self.interpreter.get_trigger_error_counters(), self.histograming.get_rel_bcid_hist()
            self.data_ready.emit(interpreted_data, [data[1], data[2], data[3], data[4], self.n_hits])
        elif data and len(data) == 2:  # data is a config object, thus reset and prepare interpreter according to the new settings
            if data[0] == 'RunConf':  # run conf is set at the beginning of a run
                self.run_start.emit()
                try:
                    n_bcid = int(data[1]['trigger_count'])
                except KeyError:
                    n_bcid = 0
                self.reset_data()
                self.interpreter.set_trig_count(n_bcid)
            self.config_changed.emit(data[1])

        QtCore.QTimer.singleShot(0.0001, self.process_data)

    def __del__(self):
        self.socket.close()
        self.context.term()


class OnlineMonitorApplication(Qt.QApplication):

    def __init__(self, args, socket_addr):
        Qt.QApplication.__init__(self, args)
        self.setup_plots()
        self.setup_data_worker(socket_addr)
        self.add_widgets()
        self.fps = 0
        self.hps = 0  # hits per second
        self.plot_delay = 0
        self.updateTime = ptime.time()
        self.thread.start()
        self.exec_()

    def setup_data_worker(self, socket_addr):
        self.thread = QtCore.QThread()  # no parent!
        self.worker_data = DataWorker()  # no parent!
        self.worker_data.data_ready.connect(self.update_monitor)
        self.worker_data.run_start.connect(self.reset_config_text)
        self.worker_data.config_changed.connect(self.setup_config_text)
        self.worker_data.moveToThread(self.thread)
        self.worker_data.connect(socket_addr)
        self.thread.started.connect(self.worker_data.process_data)
        self.aboutToQuit.connect(self.thread.quit)

    def setup_plots(self):
        pg.setConfigOption('background', 'w')
        pg.setConfigOption('foreground', 'k')

    def reset_config_text(self):
        self.run_conf_list_widget.clear()
        self.run_conf_list_widget.addItem(Qt.QListWidgetItem("Configuration"))

    def setup_config_text(self, config):
        for key, value in config.iteritems():
            item = Qt.QListWidgetItem("%s: %s" % (key, value))
            self.run_conf_list_widget.addItem(item)

    def add_widgets(self):
        # Main window with dock area
        self.window = QtGui.QMainWindow()
        self.dock_area = DockArea()
        self.window.setCentralWidget(self.dock_area)
        self.window.resize(800, 840)
        self.window.setWindowTitle('Online Monitor')
        self.window.show()

        # Docks
        dock_occcupancy = Dock("Occupancy", size=(400, 400))
        dock_run_config = Dock("Configuration", size=(400, 400))
        dock_tot = Dock("Time over threshold values (TOT)", size=(400, 400))
        dock_tdc = Dock("Time digital converter values (TDC)", size=(400, 400))
        dock_event_status = Dock("Event status", size=(400, 400))
        dock_trigger_status = Dock("Trigger status", size=(400, 400))
        dock_service_records = Dock("Service records", size=(400, 400))
        dock_hit_timing = Dock("Hit timing (rel. BCID)", size=(400, 400))
        dock_status = Dock("Status", size=(800, 40))
        self.dock_area.addDock(dock_run_config, 'left')
        self.dock_area.addDock(dock_occcupancy, 'above', dock_run_config)
        self.dock_area.addDock(dock_tdc, 'right', dock_occcupancy)
        self.dock_area.addDock(dock_tot, 'above', dock_tdc)
        self.dock_area.addDock(dock_service_records, 'bottom', dock_occcupancy)
        self.dock_area.addDock(dock_trigger_status, 'above', dock_service_records)
        self.dock_area.addDock(dock_event_status, 'above', dock_trigger_status)
        self.dock_area.addDock(dock_hit_timing, 'bottom', dock_tot)
        self.dock_area.addDock(dock_status, 'top')

        # Status widget
        cw = QtGui.QWidget()
        cw.setStyleSheet("QWidget {background-color:white}")
        layout = QtGui.QGridLayout()
        cw.setLayout(layout)
        self.rate_label = QtGui.QLabel("Readouts")
        self.hit_rate_label = QtGui.QLabel("Hits")
        self.timestamp_label = QtGui.QLabel("Data timestamp")
        self.plot_timestamp_label = QtGui.QLabel("Plot timestamp")
        self.plot_delay_label = QtGui.QLabel("Plot delay")
        self.scan_parameter_label = QtGui.QLabel("Scan parameter")
        self.spin_box = Qt.QSpinBox(value=1)
        self.spin_box.valueChanged.connect(self.worker_data.set_integrate_readouts)
        layout.addWidget(self.timestamp_label, 0, 0, 0, 1)
        layout.addWidget(self.plot_timestamp_label, 0, 1, 0, 1)
        layout.addWidget(self.plot_delay_label, 0, 2, 0, 1)
        layout.addWidget(self.rate_label, 0, 3, 0, 1)
        layout.addWidget(self.hit_rate_label, 0, 4, 0, 1)
        layout.addWidget(self.scan_parameter_label, 0, 5, 0, 1)
        layout.addWidget(self.spin_box, 0, 6, 0, 1)
        dock_status.addWidget(cw)

        # Config dock
        self.run_conf_list_widget = Qt.QListWidget()
        dock_run_config.addWidget(self.run_conf_list_widget)

        # Different plot docks

        occupancy_graphics = pg.GraphicsLayoutWidget()
        occupancy_graphics.show()
        view = occupancy_graphics.addViewBox()
        self.occupancy_img = pg.ImageItem(border='w')
        view.addItem(self.occupancy_img)
        view.setRange(QtCore.QRectF(0, 0, 80, 336))
        dock_occcupancy.addWidget(occupancy_graphics)

        tot_plot_widget = pg.PlotWidget(background="w")
        self.tot_plot = tot_plot_widget.plot(np.linspace(-0.5, 15.5, 17), np.zeros((16)), stepMode=True)
        tot_plot_widget.showGrid(y=True)
        dock_tot.addWidget(tot_plot_widget)

        tdc_plot_widget = pg.PlotWidget(background="w")
        self.tdc_plot = tdc_plot_widget.plot(np.linspace(-0.5, 4095.5, 4097), np.zeros((4096)), stepMode=True)
        tdc_plot_widget.showGrid(y=True)
        tdc_plot_widget.setXRange(0, 800, update=True)
        dock_tdc.addWidget(tdc_plot_widget)

        event_status_widget = pg.PlotWidget()
        self.event_status_plot = event_status_widget.plot(np.linspace(-0.5, 15.5, 17), np.zeros((16)), stepMode=True)
        event_status_widget.showGrid(y=True)
        dock_event_status.addWidget(event_status_widget)

        trigger_status_widget = pg.PlotWidget()
        self.trigger_status_plot = trigger_status_widget.plot(np.linspace(-0.5, 7.5, 9), np.zeros((8)), stepMode=True)
        trigger_status_widget.showGrid(y=True)
        dock_trigger_status.addWidget(trigger_status_widget)

        service_record_widget = pg.PlotWidget()
        self.service_record_plot = service_record_widget.plot(np.linspace(-0.5, 31.5, 33), np.zeros((32)), stepMode=True)
        service_record_widget.showGrid(y=True)
        dock_service_records.addWidget(service_record_widget)

        hit_timing_widget = pg.PlotWidget()
        self.hit_timing_plot = hit_timing_widget.plot(np.linspace(-0.5, 15.5, 17), np.zeros((16)), stepMode=True)
        hit_timing_widget.showGrid(y=True)
        dock_hit_timing.addWidget(hit_timing_widget)

    def update_monitor(self, data, meta_data):
        self.timestamp_label.setText("Data timestamp\n%s" % time.asctime(time.localtime(meta_data[1])))
        self.scan_parameter_label.setText("Scan parameter\n%s" % str(meta_data[3]))
        self.update_plots(data)
        now = ptime.time()
        self.plot_timestamp_label.setText("Plot timestamp\n%s" % time.asctime(time.localtime(now)))
        self.plot_delay = self.plot_delay * 0.9 + (now - meta_data[1]) * 0.1
        self.plot_delay_label.setText("Plot delay\n%s" % ((time.strftime('%H:%M:%S', time.gmtime(self.plot_delay))) if self.plot_delay > 5 else "%1.2f ms" % (self.plot_delay * 1.e3)))
        fps2 = 1.0 / (now - self.updateTime)
        self.updateTime = now
        self.fps = self.fps * 0.7 + fps2 * 0.3  # running mean for filtering
        self.rate_label.setText("Readouts\n%d Hz" % self.fps)
        if self.spin_box.value() == 0:  # show number of hits if all hits are integrated
            self.hps = np.sum(data[0])
            self.hit_rate_label.setText("Hits\n%d" % int(self.hps))
        else:
            self.hps = self.hps * 0.95 + meta_data[4] * 0.05 * self.fps / (float(self.spin_box.value()))  # running mean for filtering
            self.hit_rate_label.setText("Hits\n%d Hz" % int(self.hps))

    def update_plots(self, data):
        self.occupancy_img.setImage(data[0][:, ::-1, 0], autoDownsample=True)
        self.tot_plot.setData(x=np.linspace(-0.5, 15.5, 17), y=data[1], fillLevel=0, brush=(0, 0, 255, 150))
        self.tdc_plot.setData(x=np.linspace(-0.5, 4096.5, 4097), y=data[2], fillLevel=0, brush=(0, 0, 255, 150))
        self.event_status_plot.setData(x=np.linspace(-0.5, 15.5, 17), y=data[3], stepMode=True, fillLevel=0, brush=(0, 0, 255, 150))
        self.service_record_plot.setData(x=np.linspace(-0.5, 31.5, 33), y=data[4], stepMode=True, fillLevel=0, brush=(0, 0, 255, 150))
        self.trigger_status_plot.setData(x=np.linspace(-0.5, 7.5, 9), y=data[5], stepMode=True, fillLevel=0, brush=(0, 0, 255, 150))
        self.hit_timing_plot.setData(x=np.linspace(-0.5, 15.5, 17), y=data[6], stepMode=True, fillLevel=0, brush=(0, 0, 255, 150))

    def __del__(self):
        self.thread.wait(1000)  # give worker thread time to stop after aboutToQuit issued thread.quit; is there a better way? stackoverflow has only worse hacks

if __name__ == '__main__':
    app = OnlineMonitorApplication(sys.argv, socket_addr='tcp://127.0.0.1:5678')
#
