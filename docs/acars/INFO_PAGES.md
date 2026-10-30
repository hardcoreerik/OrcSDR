# ACARS info pages: plain-language text

Text for the (i) information buttons on the ACARS screens (not wired into the firmware yet). Each entry has the same four lines:
**What it is**, **How we read it**, **What we are sure of**, **What we do not know**. A field that is not understood is shown exactly as
received and never given a name.

## How ACARS reaches this screen
Aircraft send short text blocks to ground stations. We listen on 136.975 MHz (VDL Mode 2, digital) and 131.550 MHz (the older analog
ACARS). Only blocks whose error check passes are shown ("CRC VERIFIED"). Nothing is sent from this device and nothing leaves it.

## Is 136.975 MHz the only digital channel?
No. 136.975 MHz is the common signalling channel, used almost everywhere, so it is the busiest and the one with a preset. Airlines and
service providers also use other 25 kHz channels in the 136.7 to 137 MHz range (136.725, 136.775, 136.825, 136.875 and 136.925 are the
usual ones; this receiver decodes digital VDL2 only from 136.725 to 136.975 MHz). Which of them carry traffic near you has to be found by listening: on the
CHANNELS screen, tap `- 25k` or `+ 25k` to step from the current frequency in 25 kHz steps (`CLEAR` returns to the preset). The
receiver listens to one channel at a time.

## Message types

**VDL2 data frame (AV)**
- What it is: a digital frame that is not an ACARS text (for example air traffic control data link or position reporting).
- How we read it: we check that the frame is intact and show its bytes as hex with the sender's 28-bit address.
- Sure of: the frame is intact and who sent it (by address).
- Do not know: what is inside. Decoding it needs the aviation standards for those applications.

**ATC data link (ARINC 622, "ATC AT1")**
- What it is: a message between the aircraft and an air traffic control centre, carried inside ACARS.
- How we read it: we check its own error check, then show the direction, the ground address, the aircraft and the size.
- Sure of: it is intact, who it is between, and its size.
- Do not know: the content. The data bits follow a standard we do not have, so they are shown as received.

**Weather (ATIS) request (label B9)**
- What it is: the crew asking for the airport weather broadcast.
- How we read it: airport, the kind of request (arrival, departure, automatic updates) and the display width, after its error check.
- Sure of: all of it; it follows a published layout and every sample checked passes.
- Do not know: nothing significant. The answer is sent back separately.

**Position reports (labels 32, 33, 22, 10, 17 and the FMC "POS" messages)**
- What it is: where the aircraft is, sent by the airline's systems.
- How we read it: positions, dates, times and airport codes describe themselves and are checked for sense. An altitude or outside
  temperature that behaves like one (right size, right trend with height) is shown with the word "probable".
- Sure of: the position and time.
- Do not know: the other numbers in the message. They are left out or shown as received.

**Flight data unit reports (H1 "DFB")**
- What it is: engine, climb and cruise data from the aircraft's data recorder, sent by the airline to its engineers.
- How we read it: the first block names the route; some blocks have an altitude, speed, Mach, temperature or position that we have
  checked against physics. Many blocks are only numbers.
- Sure of: the route, and the few columns marked as such.
- Do not know: most columns. The layouts are private to each airline and not published anywhere we found. Where we cannot read
  a block it says so and shows the raw text.

**Fault display (H1 "CFB")**
- What it is: a list of cockpit alert messages with times.
- How we read it: the report time and the entries are shown; the codes are shown as received.
- Do not know: what each code means (that needs the manufacturer's maintenance manual).

**Southwest label 37**
- What it is: a text block from Southwest aircraft that looks like scrambled characters.
- How we read it: we do not. The block is intact (it passed its error check) but the characters are not plain text, numbers or hex.
- Sure of: that it is intact and from Southwest.
- Do not know: everything about the content. It appears to be a private, scrambled format with a short per-message header. We have not
  tried to break it and no public description exists.

**T1 messages and in-range reports**
- What they are: airline system text. A T1 message names a date, time, aircraft, flight and route. An in-range report says the aircraft
  is close to its destination and when it expects to arrive.
- Sure of: the aircraft, route, day and times, which are written out in the text.
- Do not know: why a T1 message is sent or what its three-letter code means.

**Acknowledgements, link tests and ground station announcements**
- What they are: housekeeping. "ACK of block" confirms a block was received; a link test checks the radio path; a ground station
  announcement ("squitter") says which airport's station is on the channel and what services it offers.
- Sure of: all of it; these follow the published ARINC layouts.

## About the numbers on the screen
- "Decoded" counts only text we understood. A block we can only name ("flight data unit block") is not counted as decoded.
- "Probable" means the value behaves like what it is called (checked against physics or other aircraft) but no document confirms it.
