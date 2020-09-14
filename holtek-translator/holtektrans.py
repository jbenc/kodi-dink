#!/usr/bin/python
import evdev
from evdev.ecodes import *
import select

KBD_DEV_NAME = "/dev/input/by-id/usb-HOLTEK_e000-event-kbd"
MOUSE_DEV_NAME = "/dev/input/by-id/usb-HOLTEK_e000-if01-event-mouse"

FLAG_LSHIFT = 1
FLAG_LCTRL = 2
FLAG_LALT = 4
FLAG_LMETA = 8
FLAG_LMOUSE = 16

xtable = (
	(FLAG_LCTRL, KEY_I, KEY_GREEN),
	(FLAG_LALT, KEY_TAB, KEY_YELLOW),
	(FLAG_LCTRL, KEY_G, KEY_PROGRAM),
	(FLAG_LALT | FLAG_LMETA, KEY_ENTER, KEY_MENU),
	(0, BTN_RIGHT, KEY_INFO),
	(0, KEY_F8, KEY_MUTE),
	(FLAG_LCTRL, KEY_O, KEY_LIST),
	(FLAG_LCTRL, KEY_R, KEY_RECORD),
	(FLAG_LCTRL | FLAG_LSHIFT, KEY_P, KEY_PLAY),
	(FLAG_LCTRL | FLAG_LSHIFT, KEY_S, KEY_STOPCD),
	(FLAG_LCTRL, KEY_P, KEY_PLAYPAUSE),
	(FLAG_LCTRL | FLAG_LSHIFT, KEY_T, KEY_TEXT),
	)

modtable = (
	(KEY_LEFTSHIFT, FLAG_LSHIFT),
	(KEY_LEFTCTRL, FLAG_LCTRL),
	(KEY_LEFTALT, FLAG_LALT),
	(KEY_LEFTMETA, FLAG_LMETA),
	)


class MyDevice(evdev.device.InputDevice):
	mod_flags = 0
	pushed = None

	def set_mod_flag(self, flag, state):
		if state == evdev.events.KeyEvent.key_down:
			self.mod_flags |= flag
		else:
			self.mod_flags &= ~flag

	def process_event(self, ev):
		event = evdev.categorize(ev)
		if ev.type == EV_KEY:
			if event.keystate == event.key_hold:
				return None
			for (src, dst) in modtable:
				if event.scancode == src:
					self.set_mod_flag(dst, event.keystate)
					return None
			for (flags, src, dst) in xtable:
				if (self.mod_flags != flags) or (src != event.scancode):
					continue
				if event.keystate == event.key_down:
					# save the pushed button for correct
					# release translation (as the remote
					# first releases the modifier keys and
					# only after that the main key)
					self.pushed = dst
				else:
					# if there's no modifier key for the
					# source event, we match here and have
					# to reset the stored pushed key
					self.pushed = None
				return evdev.InputEvent(ev.sec, ev.usec, EV_KEY, dst, ev.value)
			if (event.keystate == event.key_up) and self.pushed:
				dst = self.pushed
				self.pushed = None
				return evdev.InputEvent(ev.sec, ev.usec, EV_KEY, dst, ev.value)
			return ev
		return None


kbd_in = MyDevice(KBD_DEV_NAME)
mouse_in = MyDevice(MOUSE_DEV_NAME)
kbd_in.grab()
mouse_in.grab()

kbd_out = evdev.UInput(name="translated-remote")

while True:
	(ready, tmp1, tmp2) = select.select((kbd_in, mouse_in), (), ())
	for dev in ready:
		for event in dev.read():
			event = dev.process_event(event)
			if not event:
				continue
			kbd_out.write_event(event)
			kbd_out.syn()
