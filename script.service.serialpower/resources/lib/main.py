from pyalsa import alsacard, alsamixer
import time
import xbmc

class SerialPowerAddon(xbmc.Monitor):
    def __init__(self, card_name, mix_name):
        super().__init__()
        self.find_card(card_name)
        self.find_mixer_element(mix_name)

    def find_card(self, name):
        for card_id in alsacard.card_list():
            if name == alsacard.card_get_name(card_id):
                self.card_id = card_id
                self.mixer = alsamixer.Mixer()
                self.mixer.attach('hw:{}'.format(self.card_id))
                self.mixer.load()
                return True
        return False

    def find_mixer_element(self, name):
        self.mix_elem = alsamixer.Element(mixer=self.mixer, name=name)

    def mute(self, state):
        self.mix_elem.set_switch(not state)

    def write_sock(self, value):
        f = open('/run/serialpower.sock', 'w')
        f.write(str(value))
        f.close()

    def speaker_on(self):
        # hack - mute the card for a short while to prevent loud crack on
        # initialization
        self.mute(True)
        self.write_sock(1)
        time.sleep(0.5)
        self.mute(False)

    def speaker_off(self):
        self.write_sock(0)

    def onNotification(self, sender, method, data):
        super().onNotification(sender, method, data)
        if method == 'Player.OnPlay':
            self.speaker_on()
        elif method == 'Player.OnStop':
            self.speaker_off()

addon = SerialPowerAddon('HDA Intel PCH', 'Master')
addon.waitForAbort()
