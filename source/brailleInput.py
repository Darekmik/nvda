#brailleInput.py
#A part of NonVisual Desktop Access (NVDA)
#This file is covered by the GNU General Public License.
#See the file COPYING for more details.
#Copyright (C) 2012-2015 NV Access Limited, Rui Batista

import os.path
import time
import louis
import braille
import config
from logHandler import log
import winUser
import inputCore
import speech
import keyboardHandler

"""Framework for handling braille input from the user.
All braille input is represented by a {BrailleInputGesture}.
Normally, all that is required is to create and execute a L{BrailleInputGesture},
as there are built-in gesture bindings for braille input.
"""

#: The singleton BrailleInputHandler instance.
#: @type: L{BrailleInputHandler}
handler = None

def initialize():
	global handler
	handler = BrailleInputHandler()
	log.info("Braille input initialized")

def terminate():
	global handler
	handler = None

class BrailleInputHandler(object):
	"""Handles braille input.
	"""

	def __init__(self):
		self.isContracted = False
		self.bufferBraille = []
		self.bufferText = u""
		#: Indexes of cells which produced text.
		#: For example, this includes letters and numbers, but not number signs,
		#: since a number sign by itself doesn't produce text.
		self.cellsWithText = set()
		self.composedBraille = ""

	def input(self, dots):
		"""Handle one cell of braille input.
		"""
		# Maintain a buffer so that state set by previous cells can be handled;
		# e.g. capital and number signs.
		oldTextLen = len(self.bufferText)
		self.bufferBraille.append(dots)

		if not self.isContracted or dots == 0:
			# Translate the buffer.
			# liblouis requires us to set the highest bit for proper use of dotsIO.
			data = u"".join([unichr(cell | 0x8000) for cell in self.bufferBraille])
			self.bufferText = louis.backTranslate(
				[os.path.join(braille.TABLES_DIR, config.conf["braille"]["inputTable"]),
				"braille-patterns.cti"],
				data, mode=louis.dotsIO)[0]
			newText = self.bufferText[oldTextLen:]
			if newText:
				if self.isContracted:
					speech._suppressSpeakTypedCharacters = (len(newText), time.time())
				else:
					self.cellsWithText.add(len(self.bufferBraille) - 1)
					self.composedBraille = ""
				self.sendChars(newText)
			else:
				self._reportNonText(dots)
		else:
			self._reportNonText(dots)

		if dots == 0: # Space
			self.flushBuffer()

	def _reportNonText(self, dots):
		if  config.conf["keyboard"]["speakTypedCharacters"]:
			speakDots(dots)
		self.composedBraille += unichr(0x2800 + dots)
		self._updateComposed()

	def _updateComposed(self):
		region = braille.handler.mainBuffer.regions[-1] if braille.handler.mainBuffer.regions else None
		if isinstance(region, braille.TextInfoRegion):
			braille.handler._doCursorMove(region)

	def eraseLastCell(self):
		if not self.bufferBraille:
			inputCore.manager.emulateGesture(keyboardHandler.KeyboardInputGesture.fromName("backspace"))
			return
		index = len(self.bufferBraille) - 1
		cell = self.bufferBraille.pop()
		if index in self.cellsWithText:
			inputCore.manager.emulateGesture(keyboardHandler.KeyboardInputGesture.fromName("backspace"))
			self.cellsWithText.remove(index)
		else:
			# This cell didn't produce text.
			speakDots(cell)
			self.composedBraille = self.composedBraille[:-1]
			self._updateComposed()

	def flushBuffer(self):
		self.bufferBraille = []
		self.bufferText = u""
		self.cellsWithText.clear()
		self.composedBraille = ""

	def getComposedInput(self):
		return "".join([0x2800 + unichr(cell) for cell in self.bufferBraille])

	def sendChars(self, chars):
		inputs = []
		for ch in chars:
			for direction in (0,winUser.KEYEVENTF_KEYUP): 
				input = winUser.Input()
				input.type = winUser.INPUT_KEYBOARD
				input.ii.ki = winUser.KeyBdInput()
				input.ii.ki.wScan = ord(ch)
				input.ii.ki.dwFlags = winUser.KEYEVENTF_UNICODE|direction
				inputs.append(input)
		winUser.SendInput(inputs)

def formatDotNumbers(dots):
	out = []
	for dot in xrange(8):
		if dots & (1 << dot):
			out.append(str(dot + 1))
	return " ".join(out)

def speakDots(dots):
	# Translators: Used when reporting braille dots to the user.
	speech.speakMessage(_("dot") + " " + formatDotNumbers(dots))

class BrailleInputGesture(inputCore.InputGesture):
	"""Input (dots and/or space bar) from a braille keyboard.
	This could either be as part of a braille display or a stand-alone unit.
	L{dots} and L{space} should be set appropriately.
	"""

	#: Bitmask of pressed dots.
	#: @type: int
	dots = 0

	#: Whether the space bar is pressed.
	#: @type: bool
	space = False

	def _makeDotsId(self):
		return "+".join("dot%d" % (i+1) for i in xrange(8) if self.dots & (1 << i))

	def _get_identifiers(self):
		if self.space and self.dots:
			return ("bk:space+%s" % self._makeDotsId(),
				"bk:space+dots")
		elif self.dots in (braille.DOT7, braille.DOT8):
			# Allow bindings to dots 7 or 8 by themselves.
			return ("bk:" + self._makeDotsId(),
				"bk:dots")
		elif self.dots or self.space:
			return ("bk:dots",)
		else:
			return ()

	def _get_displayName(self):
		if not self.dots and not self.space:
			return None
		# Translators: Reported before braille input in input help mode.
		out = _("braille") + " "
		if self.space and self.dots:
			# Translators: Reported when braille space is pressed with dots in input help mode.
			out += _("space with dot")
		elif self.dots:
			# Translators: Reported when braille dots are pressed in input help mode.
			out += _("dot")
		elif self.space:
			# Translators: Reported when braille space is pressed in input help mode.
			out += _("space")
		if self.dots:
			out += " " + formatDotNumbers(self.dots)
		return out
