#!/usr/bin/env python3

#*******************************************************************************
# Copyright (c) 2025-2026
# Author(s): Volker Fischer
#*******************************************************************************
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
# details.
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
#*******************************************************************************

# sudo apt install python3-venv python3-pip libegl1
# python3 -m venv venv
# source venv/bin/activate
# pip install PySide6 requests

import sys
import math
import requests
import csv
from datetime import datetime
from PySide6.QtWidgets import (QApplication, QDialog, QVBoxLayout, QHBoxLayout,
                               QPushButton, QLineEdit, QLabel, QTableWidget,
                               QTableWidgetItem, QAbstractItemView)
from PySide6.QtCore import Qt, QSettings, QStandardPaths, QThread, Signal, Slot, QLocale

# API Key from main.cpp
ALPHAVANTAGE_KEY = "BC4BAK7UY9DHZ7NX"

class CStock:
    """Python implementation of CStock protected class from main.h"""
    def __init__(self, sym, currency, a_class, ratio):
        self.sSym = sym
        self.eCurcy = currency # "EUR" or "USD"
        self.sAClass = a_class
        self.fRatio = ratio
        self.sName = ""
        self.iN = 0
        self.fQuote = float('nan')

    def save(self, settings):
        # Match Store() in main.h
        settings.setValue(f"stock/{self.sSym}", self.iN)
        settings.setValue(f"quote/{self.sSym}", self.fQuote)

    def recall(self, settings):
        # Match Recall() in main.h
        self.iN = int(settings.value(f"stock/{self.sSym}", 0))
        self.fQuote = float(settings.value(f"quote/{self.sSym}", float('nan')))

class PriceWorker(QThread):
    """Background thread to fetch prices to avoid freezing UI."""
    price_updated = Signal(int, float)
    finished_all = Signal()

    def __init__(self, stocks):
        super().__init__()
        self.stocks = stocks

    def run(self):
        for i, stock in enumerate(self.stocks):
            url = f"https://www.alphavantage.co/query?function=GLOBAL_QUOTE&symbol={stock.sSym}&apikey={ALPHAVANTAGE_KEY}"
            try:
                response = requests.get(url, timeout=10)
                data = response.json()
                price = data.get("Global Quote", {}).get("05. price")
                if price:
                    self.price_updated.emit(i, float(price))
            except Exception as e:
                print(f"Error fetching {stock.sSym}: {e}")
            self.msleep(500)
        self.finished_all.emit()

class StockApp(QDialog):
    def __init__(self):
        super().__init__()
        # Match organization name from main.cpp
        self.settings = QSettings(QSettings.IniFormat, QSettings.UserScope, "VoFiSoft", "Stocks")

        self.f_invest = 0.0
        self.f_total = 0.0
        self.fEurInUsd = 1.0 # Conversion factor from main.cpp/h
        self.s_trans_file = "transactions.txt"

        # Initialize stocks exactly as in main.cpp
        self.v_stocks = [
            CStock("EXX5.DE", "EUR", "LARGE_US", 11.0),
            CStock("EXW1.DE", "EUR", "LARGE_EU", 12.0),
            CStock("EXXW.DE", "EUR", "LARGE_ASIA", 5.0),
            CStock("EXSE.DE", "EUR", "SMALL_EU", 27.0),
            CStock("UIMI.DE", "EUR", "EM_MARKET", 25.0),
            CStock("IQQ6.DE", "EUR", "IMMO", 10.0),
            CStock("EXXY.DE", "EUR", "RESSOURCE", 10.0),
            CStock("EXHB.DE", "EUR", "NORISK", 0.0)
        ]

        for stock in self.v_stocks:
            stock.recall(self.settings)

        # Set desktop path for transactions as in main.cpp
        paths = QStandardPaths.standardLocations(QStandardPaths.DesktopLocation)
        if paths:
            self.s_trans_file = f"{paths[0]}/{self.s_trans_file}"

        self.init_ui()
        self.update_cur_perc()

    def init_ui(self):
        self.setWindowTitle("Stocks Portfolio Manager")
        self.resize(800, 400) # Match resize in main.cpp
        layout = QVBoxLayout(self)

        sub_layout = QHBoxLayout()
        self.edit_invest = QLineEdit()
        self.label_actual = QLabel("Actual/€: 0.00")
        self.btn_buy = QPushButton("Buy")
        self.btn_update = QPushButton("Refresh Quotes")

        sub_layout.addWidget(QLabel("Invest/€:"))
        sub_layout.addWidget(self.edit_invest)
        sub_layout.addWidget(self.label_actual)
        sub_layout.addWidget(self.btn_buy)
        sub_layout.addWidget(self.btn_update)

        self.table = QTableWidget(len(self.v_stocks), 8)
        self.table.setHorizontalHeaderLabels([
            "Class", "Name", "Quote/€", "Shares", "Diff./%",
            "New Shares", "New Diff./%", "Ratio/%"
        ])
        self.table.setSelectionBehavior(QAbstractItemView.SelectRows)

        self.label_total = QLabel("Total/€: 0.00")

        layout.addLayout(sub_layout)
        layout.addWidget(self.table)
        layout.addWidget(self.label_total)

        self.init_table_data()

        # Connections from main.cpp/h
        self.edit_invest.textChanged.connect(self.on_invest_changed)
        self.btn_buy.clicked.connect(lambda: self.update_cur_perc(True))
        self.btn_update.clicked.connect(self.start_live_update)
        self.table.itemChanged.connect(self.on_cell_edited)
        self.table.itemSelectionChanged.connect(self.update_cur_perc)

    def init_table_data(self):
        self.table.blockSignals(True)
        for i, stock in enumerate(self.v_stocks):
            # Column 0: Class (Symbol) - Match NewROTabWidIt
            self.table.setItem(i, 0, self.ro_item(f"{stock.sAClass} ({stock.sSym})"))
            self.table.setItem(i, 1, self.ro_item(stock.sName))
            self.table.setItem(i, 2, QTableWidgetItem(f"{stock.fQuote:.2f}"))
            self.table.setItem(i, 3, QTableWidgetItem(str(stock.iN)))
            self.table.setItem(i, 4, self.ro_item(""))
            self.table.setItem(i, 5, self.ro_item(""))
            self.table.setItem(i, 6, self.ro_item(""))
            self.table.setItem(i, 7, self.ro_item(f"{stock.fRatio:.0f}"))
        self.table.blockSignals(False)
        self.table.resizeColumnsToContents()

    def ro_item(self, text):
        item = QTableWidgetItem(str(text))
        item.setFlags(item.flags() ^ Qt.ItemIsEditable)
        return item

    def update_cur_perc(self, do_update=False):
        self.table.blockSignals(True)

        # 1. Total Value with Currency Check
        self.f_total = 0.0
        for s in self.v_stocks:
            if not math.isnan(s.fQuote):
                # Convert USD to EUR if necessary
                quote_in_eur = s.fQuote / self.fEurInUsd if s.eCurcy == "USD" else s.fQuote
                self.f_total += quote_in_eur * s.iN

        self.label_total.setText(f"Total/€: {self.f_total:.2f}")

        if self.f_total <= 0 and self.f_invest <= 0:
            self.table.blockSignals(False)
            return

        # 2. Rebalancing Logic
        min_idx, min_val = 0, 100.0
        max_idx, max_val = 0, -100.0

        for i, stock in enumerate(self.v_stocks):
            quote_eur = stock.fQuote / self.fEurInUsd if stock.eCurcy == "USD" else stock.fQuote
            share_val = quote_eur * stock.iN
            diff = (share_val / self.f_total * 100) - stock.fRatio if self.f_total > 0 else -stock.fRatio

            if diff < min_val: min_val, min_idx = diff, i
            if diff > max_val: max_val, max_idx = diff, i

            self.table.item(i, 4).setText(f"{diff:.2f}")

        # 3. Target Stock Selection
        cur_idx = min_idx if self.f_invest > 0 else max_idx
        selected = self.table.selectedItems()
        if selected:
            cur_idx = selected[0].row()

        # 4. Calculate New Shares
        active_stock = self.v_stocks[cur_idx]
        quote_eur = active_stock.fQuote / self.fEurInUsd if active_stock.eCurcy == "USD" else active_stock.fQuote

        new_shares = 0
        if not math.isnan(quote_eur) and quote_eur > 0:
            new_shares = math.floor(self.f_invest / quote_eur)

        self.table.item(cur_idx, 5).setText(str(new_shares))

        # 5. Preview New Diffs
        total_new = self.f_total + (quote_eur * new_shares)
        for i, stock in enumerate(self.v_stocks):
            q_eur = stock.fQuote / self.fEurInUsd if stock.eCurcy == "USD" else stock.fQuote
            new_n = stock.iN + (new_shares if i == cur_idx else 0)
            new_diff = (q_eur * new_n / total_new * 100) - stock.fRatio if total_new > 0 else 0
            self.table.item(i, 6).setText(f"{new_diff:.2f}")

        # 6. Actual Invest Label
        actual_val = new_shares * quote_eur
        self.label_actual.setText(f"Actual/€: {actual_val:.2f}")
        self.label_actual.setStyleSheet("color: red;" if actual_val > self.f_invest else "")

        # 7. Apply Purchase
        if do_update and new_shares != 0:
            self.v_stocks[cur_idx].iN += new_shares
            self.table.item(cur_idx, 3).setText(str(self.v_stocks[cur_idx].iN))
            self.update_cur_perc(False)
            self.log_transaction()

        self.table.blockSignals(False)

    def log_transaction(self):
        """Append to file exactly as LogTransactionInFile in main.cpp"""
        try:
            with open(self.s_trans_file, "a", newline='') as f:
                # CStockApp format: Date, Total, Sym, Quote, N...
                writer = csv.writer(f)
                row = [datetime.now().strftime("%Y-%m-%d"), f"{self.f_total:.2f}"]
                for s in self.v_stocks:
                    row.extend([s.sSym, f"{s.fQuote:.2f}", str(s.iN)])
                writer.writerow(row)
        except Exception as e:
            print(f"File log error: {e}")

    def on_invest_changed(self, text):
        try:
            self.f_invest = float(text) if text else 0.0
        except ValueError:
            self.f_invest = 0.0
        self.update_cur_perc()

    def on_cell_edited(self, item):
        """Match change(QTableWidgetItem*) slot in main.h"""
        row, col = item.row(), item.column()
        self.table.blockSignals(True)
        try:
            if col == 3: # Shares
                self.v_stocks[row].iN = int(item.text() or 0)
            elif col == 2: # Quote
                # Support German locale decimal comma as per C++ QLocale("DE")
                val_str = item.text().replace(',', '.')
                self.v_stocks[row].fQuote = float(val_str)
            self.update_cur_perc()
        except ValueError:
            pass
        finally:
            self.table.blockSignals(False)

    def start_live_update(self):
        self.btn_update.setEnabled(False)
        self.worker = PriceWorker(self.v_stocks)
        self.worker.price_updated.connect(self.on_live_price)
        self.worker.finished_all.connect(lambda: self.btn_update.setEnabled(True))
        self.worker.start()

    def on_live_price(self, index, price):
        self.v_stocks[index].fQuote = price
        # Get the item for the "Quote/€" column (Column 2)
        item = self.table.item(index, 2)
        if item:
            item.setText(f"{price:.2f}")
            # Create a bold font and apply it to the item
            font = item.font()
            font.setBold(True)
            item.setFont(font)
        self.update_cur_perc()

    def closeEvent(self, event):
        """Match ~CStockApp() destructor to save settings"""
        for s in self.v_stocks:
            s.save(self.settings)
        event.accept()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = StockApp()
    window.show()
    sys.exit(app.exec())
