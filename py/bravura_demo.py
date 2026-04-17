import sys
from PyQt5.QtWidgets import QApplication, QLabel
from PyQt5.QtGui import QFont

app = QApplication(sys.argv)

sharp = "\uE262"
flat = "\uE260"

label = QLabel(f"{sharp}  {flat}")
label.setFont(QFont("Bravura", 72))
label.resize(300, 200)
label.show()

sys.exit(app.exec())
