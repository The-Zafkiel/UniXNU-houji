#!/usr/bin/env python3
"""Run the selected active v1 transport and extended-record ABI check."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).resolve().parents[1]/'turnip-v1-transport.py'),run_name='__main__')
