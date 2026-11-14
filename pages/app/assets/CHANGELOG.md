## v1.51.1 - 09/November/2026
- fixed a bug where contacts with a 3-byte path would always reset to flood
- fixed a bug where setting dm flood attempts to zero would show error message when a contact had no path
- macos builds are now codesigned and notarized

## v1.51.0 - 04/November/2026
- added new quick messages feature to message composer menu (aka canned messages)
- added new button to map screens to jump to your current gps position (uses phone gps)
- added new buzzer alerts checkbox to notification settings screen to turn companion buzzer on and off (requires companion firmware v1.19.0+)
- added new message settings to configure how many direct and flood attempts when sending direct messages
- added info banner to make it more apparent that sending contact messages requires other person to add you as contact first
- added ability to cancel retries when sending an outbound contact message
- added button to forget last advert timestamp from contact details screen
- added ability to long press discover map to update self position and copy coordinates
- added map marker on discover map for own companion node position
- added new popup ui to add contacts via deep links
- contact messages now attempt 4 direct and 4 flood instead of 4 direct and 1 flood
- hop count and path hash sizes are now shown under channel messages by default
- messages are now marked as failed if trying to resend when disconnected
- fixed bug where trying to resend message while disconnected would delete the message
- macos 12+ is now required

## v1.50.0 - 25/September/2026
- added command line button to companion contact details to remote manage other companion nodes
- added remote cli permission to contact permissions screen, to allow other nodes to remote manage your node
- added support for local companion command line in settings screen, requires companion firmware v1.18.0+
- added ability to import and export region scopes in companion config json
- added new wifi settings screen to configure ssid and password
- added connection type being used to bottom of settings screen
- added button to cluster repeater markers on path trace map tool
- region scopes are now shown in channels list if configured
- channel message retention duration is now included in companion config export
- long pressing ping button now configures the default path hash size for all future pings
- tracing 3byte paths is not supported by protocol, app now converts to 2byte when tracing repeater path
- ping failed error message now shows repeater name and prefix
- qr codes are now always black on a white background
- fixed bug where notifications were shown for cli data received from other companions
- fixed bug where new line key was not working on some android devices
- fixed bug where app would crash on graphene os after viewing repeater login
- fixed bug where contact and channel search bars were not cleared when node reconnected
- fixed bug where some esp32 devices would timeout fetching device info when connecting via usb on windows
- updated wording of unlocking remote management on all devices to be explicit about the platform

## v1.49.0 - 22/August/2026
- added ability to trace current path to and from repeater
- added new message setting to show region names for received channel messages
- added new network settings screen to repeater remote management to configure path hash size and default region scope
- added icon on channel list item to show if notifications are turned off
- notification badge is now grey instead of red for channels that have notifications muted
- unread indicator on contacts and channels tabs now shows unread conversations count
- moved path hash size and default region scope settings from experimental settings to main settings screen
- radio presets now include a default path hash size and will be applied when switching presets
- path hash size can now be configured in advanced settings during onboard flow
- disabled autocorrect for region name field
- fixed bug where unread messages indicator sometimes wouldn't show if unread contact was off screen
- fixed bug in map where app could freeze when zooming quickly

## v1.48.0 - 15/August/2026
- added ability to view regions of discovered repeaters
- added ability to resend previous cli commands from long press menu
- added button to resume existing session instead of requiring log in to repeater every time
- added support for push notifications on web, please enable permission in browser settings
- added menu item to copy contacts and channels as link to clipboard from share screen
- added ability to show/hide specific telemetry channel sections
- all telemetry is now shown on one page and the telemetry channel dropdown has been removed
- power telemetry now also shows power calculated from voltage and current if it differs from power telemetry value
- pressing back button when viewing channels or map tab on main screen now goes to contacts tab
- config export file now includes channel scope and scope is applied when importing config file
- disabled autocorrect for command line input field
- map marker labels now abbreviate large group counts
- improved performance of internet map loading
- updated from flutter v3.29.0 to v3.44.9
- updated internal libraries to comply with new apple/google policies
- unfortunately due to apple requirements ios 15+ and macos 12+ is now required
- unfortunately due to google requirements android 7+ is now required
- fixed bug where copied to clipboard text wasn't using translated string
- fixed bug where add contact button in discover screen wasn't using translated string
- fixed bug where scrolling at end of antenna coverage layers list would zoom map on web

## v1.47.0 - 10/July/2026
- added new map tab to path viewer screen
- added path hash size to path viewer subtitle
- added ability to share channels from message composer
- added ability to click meshcore:// urls in external apps
- added ability to scan meshcore qr codes in system camera app
- added ability to attach region scopes to qr code when sharing channel
- added ability to fetch owner info and request name from repeaters in nearby discovery tool
- added ability to select lora bandwidths supported by 2.4GHz radios
- tapping nearby discovered node can now open discovered contact screen if available
- moved message sender name inside message bubble to help with dark coloured names in dark mode
- failed to sync messages error popup has been removed

## v1.46.0 - 30/June/2026
- added new channel settings screen
- added catalan and swedish translations
- added ability to auto delete channel messages after selected retention period
- added ability to select sensors when sharing a contact from message composer
- added new message setting checkbox to auto focus message composer input field
- added validation check to latitude/longitude before showing contacts on local map
- significantly improved background bluetooth connections on ios by fixing bug with ble state restoration
- moved menu items to rename channel, view participants and blocked users to new channel settings screen
- fixed bug on iPadOS 26.1+ where dropdown menu closes instantly if tapped near status bar
- increased border radius on popup menus from 2px to 12px
- updated translations

## v1.45.0 - 14/June/2026
- added new onboarding ui for initial device setup
- added support for discovering regions from non contacts in firmware v1.16.0+
- added button to add contact when contacts list is empty
- fixed bug where dialog crashed app if both dialog buttons pressed at same time
- fixed bug where channel wouldn't load if firmware had multiple channels with the same secret
- fixed bug where neighbours map didn't include self repeater when fitting map markers
- fixed bug where adding current position to message didn't actually fetch updated position
- fixed bug where filtering discover list by path hash size ignored contact type filter
- fixed bug where android foreground service could cause app crash
- updated icon for channels tab from signal to hashtag
- updated translations

## v1.44.0 - 20/May/2026
- added support for usb connections on windows, macos and linux
- added checkbox to allow auto reconnect on wifi connections
- added ability to send unscoped channel messages when default scope configured (firmware v1.16.0+)
- added auto link support in chat messages for latitude,longitude with at least 3 decimals
- added snr signal strength support for sf5 and sf6
- added language translations for spanish and romanian
- redesigned message composer ui with ability to share contacts and positions
- typing @username in channel messages now auto adds a space afterwards
- repeater neighbours list can now resolve names from discovered contacts database
- repeater neighbours can now show on map tab even if the repeater you're logged into has no position
- user is no longer forced to change frequency if it's already allowed when turning on repeat mode
- draft messages are now saved by default this can be turned off in message settings
- improved pairing flow for bluetooth companions on windows
- fixed bug where some commands would not time out
- fixed bug where connect screen would not close when node auto reconnected
- fixed bug where deleting an outgoing message would keep retrying even if delivered

## v1.43.0 - 18/April/2026
- added ability to export app sqlite database
- added ability to search dms and channel messages
- added button to quick scroll to bottom of conversation
- added ability to filter discover list and discover map by advert path hash size
- added checkbox to path viewer screens to use discover list for resolving known nodes
- added ability to configure default region for companion/repeater firmware v1.15.0+
- added confirmation dialog before denying flood for unscoped wildcard region
- added new message setting to show/hide path hash size under channel messages
- added czech, portuguese, russian and ukrainian translations
- accessing discover contacts screen is now allowed when node is disconnected
- regions ui in channels now shows if a default region is in use
- improved performance of importing discover list from json file
- improved user experience of companion repeat mode by allowing selection of frequency when enabling
- menu buttons to select all contacts now only selects all from search results
- adding repeater region now detects if region has flood enabled by default in v1.15.0+ repeater firmware
- fixed bug where success dialogs were shown even if user cancelled file save
- fixed bug where clicking outside of ping warning dialog on android would not finish loading state
- fixed bug where region management would break if region name was "unknown"
- fixed bug where add region screen auto capitalized text input

## v1.42.0 - 22/March/2026
- added ability to long press ping buttons to use multibyte ping
- added ability to open line of sight tool from coverage map tool menu
- added ability to import and export discovered contacts as json from discover screen menu
- added menu buttons to select all and deselect all contacts while in multi select mode
- added radio checkbox to select region screen to make it more apparent you must select the region to use
- added translations for dutch, french, hungarian, italian, polish and slovak
- antenna coverage tool now automatically fits map to imported json layers
- spaces are now allowed and automatically trimmed from path field in manual trace tool
- improved error message when unable to share contact to internet map due to missing advert blob
- channel messages must now be 10 bytes shorter if a region scope is applied
- fixed bug where contacts full notification would show even when preference was turned off
- fixed bug where adding contacts from discover wouldn't copy over the last advert timestamp
- fixed bug where it wasn't possible to swipe back to exit discover screen on ios

## v1.41.0 - 04/March/2026
- added support for parsing new multi-byte path hashes in rxlog
- added support for setting 1, 2 and 3-byte paths in the set contact path tool (requires firmware v1.14.0+)
- added dropdown to select preferred path hash size in experimental settings page (requires firmware v1.14.0+)
- added path hash to unknown repeater names
- added path hash size info to contact details screens
- added button to repeater neighbours menu to quickly discover nearby neighbours
- added button to repeater neighbours menu to remove all known neighbours
- added dropdown menu button to view owner info as guest when editing as admin
- added support for new max hop limit for auto adding contacts (requires firmware v1.14.0+)
- added initial chinese, german, japanese and korean language translations
- added setting to select preferred app language
- app now allows access to repeater neighbours ui when empty as we now have a discover neighbours button
- button to delete neighbours is no longer shown when logged in as guest

## v1.40.0 - 15/February/2026
- added support for new companion repeat mode (requires companion firmware v1.13.0+)
- added new messages setting to jump to the oldest unread message when opening a conversation
- added new messages setting to keep the screen on while viewing a conversation
- added support for multi-byte path segments in path trace tools (all repeaters must be on firmware v1.11.0+)
- added dropdown menu button to manual trace tool to auto add return path
- added red info text to manual trace list if multiple repeaters are known for hop
- added button to save regions via remote management
- added info banner to repeater region management when there are unsaved changes
- added new long press menu to local and internet map screen
- added 1 second delay after sending message before allowing another message to send
- connect screen tabs now default to showing last used connection type
- removed connection options dropdown menu from connect screen (use the tabs instead)
- repeater region management ui now shows error when failing to fetch list
- direct messages now have additional random delay between retry attempts
- fixed bug where popup menus were light themed in dark mode
- fixed bug where rx log wasn't parsing advert feature flags

## v1.39.0 - 08/February/2026
- added seconds to rxlog timestamps
- added ability to manage repeater regions via remote management
- added ability to discover regions from nearby repeaters in select region menu
- added ability to dismiss repeater region info banner on channel messages screen
- added ability to change repeater identity keys and choose new prefix via remote management
- added new received packet errors field to repeater status screen
- added experimental setting to use companion clock for packet timestamps
- coverage tool now tells you if there is no elevation data for selected point
- hashtag symbol is no longer required when adding region scopes
- tx power is now read as signed int as new firmware will allow negative tx power
- app will now only process v1 packets in preparation for new v2 packet formats

## v1.38.0 - 19/January/2026
- added realtime noise floor viewer tool
- added ability to select which contact types are auto added
- added ability to scope channel messages to local repeater regions
- added new notification that tells you if contacts list is full
- added ability to configure repeater owner info via remote management
- added ability to fetch repeater owner info from guest tools
- added ability to filter contacts list by sensors
- added ability to filter by contact type in discover list
- added button to manage auto add settings from discover screen menu
- added support for email auto linking
- removed banner on discover screen saying contacts are auto added
- removed intel suffix from macos builds as they are universal binaries with intel and apple silicon support
- fixed bug where macos builds were missing camera and photo library permissions for scanning qr codes

## v1.37.0 - 04/January/2026
- added ble, usb and wifi tabs to connect screen
- added initial support for usb connections on android
- added initial support for standalone linux builds with wifi/tcp connections
- added ability to forget a contacts position from details screen
- added ability to quick remove added contact by clicking the tick in discover list
- added checkbox to discover menu to show/hide already added contacts
- channel syncing performance greatly improved with new experimental setting
- multiple simultaneous channel sync operations are now prevented with a mutex
- reduced duration of copied to clipboard popup from 5 seconds to 2 seconds
- added support for more telemetry types:
  - LPP_ANALOG_INPUT
  - LPP_ANALOG_OUTPUT
  - LPP_DIGITAL_INPUT
  - LPP_DIGITAL_OUTPUT
  - LPP_DIRECTION
  - LPP_DISTANCE
  - LPP_ENERGY
  - LPP_SWITCH
  - LPP_UNIXTIME

## v1.36.0 - 27/December/2025
- added ability to order channels alphabetically or by most recent messages
- added confirmation dialog with information about sharing position in adverts
- added ability to scroll to top of contacts/channels list by tapping the same tab a second time
- added new messages setting to show/hide hop count under channel messages
- added new messages setting to mark outgoing messages delivered faster
- app can now mark messages delivered before an ack has passed through all intended repeaters
- app is now able to minimize itself instead of terminating when pressing back button on android
- contact position is now updated if telemetry response has gps data
- fixed bug where auto reconnecting to companion would go back a screen

## v1.35.0 - 09/December/2025
- added support for standalone altitude in telemetry response
- added button to send channel message again if no repeats heard
- added ability to see last heard for rf map and discover map markers
- added dropdown menu button to remote management settings tab to reset path
- added new contact settings checkbox to show/hide public key in contacts list
- wifi connection history is now saved to allow quick connecting to previous devices
- trace map now auto hides non-selected repeaters when running trace
- leading and trailing whitespace is now removed when saving node names and creating channels
- fixed bug where outbound snr value in discover nearby nodes tool would show 54+ SNR
- fixed bug where deleting channel message history would still show last message timestamp
- fixed bug where message date divider would not show if first message of new day is unseen

## v1.34.0 - 16/November/2025
- added button to qr code scanner screen to pick from existing photos
- added dropdown menu button to copy path on view path screen
- added new tool to discover nearby nodes on firmware v1.10.0+
- added ability to navigate back a screen on desktop with mouse back button
- added first byte to hop info popup in map trace results
- upserting contacts and channels to database after sync is now faster
- rx log now parses multipart packet types
- rx log now shows channel name and message if able to decrypt
- rx log and debug logs now shows clear logs button instead of copy button in main bar
- fixed bug where coverage layers would not use antenna height from imported json
- fixed bug where android foreground service showed white notification icon

## v1.33.0 - 11/November/2025
- added new rx log tool to view received packets
- added ability to save and restore draft messages
- added new message settings checkbox to enable/disable draft message save/restore
- added new menu button to line of sight tool to add point by latitude/longitude
- added dropdown menu to line of sight and antenna coverage points
- added new screen to view channel participants
- added support for lora spreading factor 5 and 6
- added contact avatar to blocked channel senders list
- saving settings now tells you what settings failed to save
- coverage layer markers are now grey while loading or hidden
- updated labels on antenna coverage settings and line of sight settings
- fixed bug where notification icon was a white circle on android 16+
- fixed bug where clearing map trace wouldn't allow new trace until previous trace timed out

## v1.32.0 - 21/October/2025
- added connected device name to tab title in web app
- added ability to keep favourite contacts when purging
- added ability to turn off automatic error reporting to bugsnag
- added button to edit individual layer settings in coverage tool
- added contact type info to new contact discovered notifications
- major improvements to bluetooth background connections on android and ios
- ble devices are no longer force disconnected when failing to load device info
- disabled session tracking that was enabled by default in bugsnag

## v1.31.0 - 13/October/2025
- added support for frequency in telemetry response
- added ability to order discover list by hop count
- added ability to manage room server access control
- added ability to quickly enable and disable read only mode for room server
- message input field is now hidden if user doesn't have permission to post in room server
- dms and room server messages are now limited to 150 characters
- internet map search now includes the < and > characters in search results, to allow searching by prefix e.g: "<abcd"
- disabled automatic android app backups to prevent app force closing every 24 hours
- fixed bug where #hashtag channel names with uppercase letters would not render in ui
- fixed bug where internet map search results were not constrained to selected filter
- fixed bug where spamming send button would send the message multiple times

## v1.30.0 - 07/October/2025
- added map tab to discover screen
- added button to remove contact from details screen
- added distance away to popup when tapping neighbour map markers
- added ability to long press to multi select discover list items to delete
- added fahrenheit conversion to temperature telemetry items
- added ability to import contacts from clipboard links containing meshcore://contact/add
- added divider under disconnect menu button
- tapping discovered contact now shows contact info
- changing admin/guest password now enforces limit of 15 characters
- map search now includes the < and > characters in search results, to allow searching by prefix e.g: "<abcd"

## v1.29.0 - 28/September/2025
- added button to view contact on map from contact details screen
- added ability to add point in coverage tool by entering latitude/longitude
- added support for new paginated repeater neighbours (requires updating companion and repeater firmware)
- added new guest tools tab to repeater management when logged in as guest
- added ability to manage access control list for repeaters
- pressing back button now exits contact multi select mode
- message length counter now counts emojis correctly when typing direct or channel messages
- fixed bug where importing coverage layers would not use the antenna height from json file

## v1.28.0 - 21/September/2025
- added ability to join public #hashtag channels (these use deterministic encryption keys that are not private)
- added ability to tap #hashtag channels in message content to navigate to them
- added repeaters layer to line of sight and antenna coverage map tools
- added ability to tap repeaters in line of sight and coverage tools to quickly use that point
- reworked antenna coverage tool to allow multiple coverage layers, and added layer importing/exporting
- exporting contacts now includes the custom contact name in the export file
- importing contacts from config file now sets the custom name on the contact
- app now syncs relevant contact when a new message is received
- the next firmware release will update lastmod when a message is received to push contact to the top

## v1.27.0 - 11/September/2025
- added new date dividers between messages sent on different days
- added new messages divider to conversations with unread messages
- added ability to tag users in channel messages by long press to reply, or by typing @
- added new channel notification settings: "All Messages", "Mentions Only" or "None"
- added new highlighted background to channel messages when you have been tagged in the message
- added extra horizontal padding to unread messages badge when more than 99 unread
- companion radio names can no longer include the square brackets
- links in chat messages now open in system browser instead of embedded web view
- fixed bug where clicking notification when viewing link in embedded browser would launch a new app instance
- fixed bug where bluetooth scanning on older android devices would not show error message about location services
- fixed bug where notifications would not dismiss when clicking notification for conversation that's already open

## v1.26.0 - 09/September/2025
- added ability to long press contacts list to export or delete multiple contacts
- added new contact setting to enable/disable pull to refresh on contacts list
- added ability to set a custom name on contact details screen
- clicking sensor in contacts list now navigates to contact details screen instead of login
- discover list now includes the < and > characters in search results, to allow searching by prefix e.g: "<abcd"
- refactored contact syncing to fix timeouts when syncing 300+ contacts
- fixed bug where sorting contacts alphabetically would show in wrong order with mixed case names
- fixed bug where failed channel sync could reset channel notification preferences

## v1.25.0 - 28/August/2025
- added new antenna coverage tool
- added contact and channel sync progress to main app bar
- added contact name labels to main map and internet map
- added ability to refresh repeater neighbours list and map
- added new setting to select light theme, dark theme or auto based on system
- added button to remove neighbour from list by long pressing (requires experimental firmware)
- added initial experimental support for a native app on windows and macos
- pressing enter on a hardware keyboard now sends message on all platforms, press shift+enter to add new line
- app no longer allows setting a name that is longer than 31 bytes
- improved title bar ui for channel messages
- removed repeat settings ui from room server settings
- fixed bug where contact names with broken emoji would show as unknown name
- fixed bug where titles on web were incorrectly aligned

## v1.24.0 - 20/August/2025
- added new line of sight map tool
- added elevation shadow to map markers
- added link to official youtube channel in about screen
- added nz topo tile layer
- added google hybrid tile layer for web app only
- added new repeat settings button to remote management to quickly enable/disable repeat mode
- name field is now limited to 31 characters when remote managing repeater, room or sensor
- contact labels are now enabled by default on trace map
- snr and contact labels are now enabled by default on repeater neighbours map
- tools menu can now be accessed without a companion node being connected
- unread badges now show full unread count instead of stopping at 99
- line of sight map tool now defaults to companion position if available
- fixed bug where open topo map tiles would not load
- fixed bug where user could enter more characters in name field than firmware allows

## v1.23.0 - 10/August/2025
- added search bar to channels tab
- added new info text to app bar when device is syncing after device connects
- added pull to refresh support to channels tab
- added path info to login button
- added ability to long press login button to quickly login direct or flood
- added new ui design for snr labels
- ios app no longer requires ios 15.5+, it now works on ios 12+
- message notifications are now automatically dismissed when opening a conversation
- minor ui improvements for smaller screens such as ipod touch and iphone 5s
- fixed bug where ios app would crash on ios 12, 13 and 14

## v1.22.0 - 08/August/2025
- added new total rx airtime info to repeater stats
- added info about sender and receiver to advert path viewer
- added support for new multi acks in message settings screen
- added button to show labels for markers on map trace tool
- added ability to choose radio preset for repeaters, room servers and sensors via remote management
- radio settings dropdown has been replaced with a choose preset button
- if public channel exists it's now always shown at the top of channels list
- custom channels now show a lock icon to indicate that they are private channels
- chat contacts are no longer counted for ping warning when multiple contacts have same first byte
- previous database is loaded when relaunching app so you can now see message history without connecting to node
- importing contacts now uses last modified timestamp from imported config file instead of current timestamp
- improved ui of map marker labels
- fixed bug where importing contacts list would fail if your companion was in the list
- fixed bug where contacts limit in settings would show negative number when limit higher than 127
- fixed bug where system font scaling would break map marker labels

## v1.21.0 - 23/July/2025
- added ability to see channel message paths on long press
- added ability to view incoming advert path by long pressing on discover screen 
- added ability to long press send button to select direct or flood when sending a message to a contact
- added info banner to discover screen saying if device is set to auto add contacts
- contacts list now includes the < and > characters in search results, to allow searching by prefix e.g: "<abcd"
- heard repeats list now shows hop count and full path on tap
- path info list now shows red text if multiple repeaters are known for first byte
- fixed bug where it was not possible to copy/paste in search bar
- fixed bug where using radio settings from internet map would not work if they had decimal bandwidth
- fixed bug where discover list would not update timestamp if it already existed

## v1.20.0 - 17/July/2025
- added initial support for the new sensor firmware
- added ability to manage access control list for sensors
- added ability to factory reset your companion device from settings screen
- added generic sensor, concentration, percentage and presence telemetry types to ui
- added new drop down menu to repeater stats tab to toggle auto fetching after login
- repeater stats are no longer auto fetched after login to save bandwidth
- improved error message when trying to share contact with missing advert info
- system preference for 12hr or 24hr clock format is now respected by the app
- command line no longer dismisses keyboard or focus when sending a command
- fixed bug where drop down menu to clear room server command history was shown on wrong tab

## v1.19.0 - 08/July/2025
- added ability to click http:// and https:// urls in messages
- added contact avatars and coloured names to room server messages
- added info message when trying to upload self to internet map when sharing position in adverts is disabled
- added release dates to change log
- reduced wait timer from 15 seconds to 10 seconds on premium feature screen
- tapping a repeater in contacts list now goes to contact details screen if user hasn't purchased remote management
- removed old setting that allowed disabling coloured names, now that avatars are used everywhere
- improved message delivery reports by marking as delivered when a previous attempt ack comes in late
- updated links in about screen for new discord server
- fixed issue where radio frequency would show extra numbers, these are now trimmed off
- fixed bug on premium feature screen where the restore purchases button was shown under navigation bar
- fixed bug where hop counts greater than 10 would get cut off in map trace ui

## v1.18.1 - 29/June/2025
- added new setting available in firmware v1.7.1+ to configure if you want to include position in your adverts
- added ability to view which repeaters repeated a channel message in the long press menu
- fixed bug where storage usage bar wasn't visible in dark mode
- fixed bug where some invalid characters in device name would crash app when exporting config

## v1.18.0 - 22/June/2025
- added search bar to discover contacts screen
- added quick action buttons to contact details screen
- added contact avatars to local map and contact qr code screen
- added new signal strength bar in path trace list and repeater neighbours list
- added ability to block channel message senders by long pressing channel message
- added button to public info section of settings screen to quickly clear position
- added ability to disable map clustering on local and internet maps from filter menu
- added storage space used to settings screen if companion device supports it
- discover screen now shows if you already have the node in your contacts
- latitude and longitude fields on contact details screen is now a single position field
- tapping a contact on local map now goes to contact details screen
- tapping repeaters in known repeater list now goes to contact details screen
- layers button on maps now shows a dropdown menu to pick tile layer
- adjustments to ble scanning to help connecting on android 7.1
- fixed bug where contact syncing could get stuck until app is force closed
- fixed bug where foreground service would stop when reconnecting failed when coming back into range of ble device
- fixed bug where repeater neighbours list would show as unknown repeater if it didn't have a valid position

## v1.17.0 - 11/June/2025
- added ability to import and export identity key in config backup
- added ability to search nodes on local map and internet map
- added ability to filter nodes on internet map by type, and by those that you uploaded
- added name and public key of user that uploaded internet map nodes to the info popup
- added timestamp of when an internet map node was last updated to the info popup
- added select all and deselect all buttons to config import and export screens
- message notifications no longer open duplicate screen if already viewing that conversation
- notifications are now grouped together, such as dms from the same contact, or messages in the same channel
- managing identity key is now available by default in firmware v1.7.0+
- simplified user experience for filtering local map, and added filter by favourites
- error is now shown when scanning for bluetooth devices fails
- fixed bug where contact icon colours were different on web app
- fixed bug where search bar keyboard would not dismiss when navigating away

## v1.16.2 - 04/June/2025
- fixed bug where my telemetry screen showed grey screen
- force disconnect ble connections after confirming app exit
- force disconnect ble connections when scanning for available devices

## v1.16.1 - 03/June/2025
- fixed bug on ios where contact avatar was on left and name was centered

## v1.16.0 - 03/June/2025
- added ability to set contacts as favourites
- added ability to filter contacts by favourites only
- added ability to view own telemetry from settings screen
- added new "heard x repeats" to channel messages when a repeat is heard by your companion radio
- added view telemetry button to contact details screen for repeaters
- added contact avatar to contact messages app bar and improved app bar text
- added repeater clock from login response to the repeater status page
- added support for new noise floor stats for repeaters and room servers
- added distance between hops to info popup when tapping snr bubble in map path trace tool
- added dynamic contact avatars to discover list
- improved error message popup when clock cannot go backwards
- max channel message length is now calculated based on your device name
- simplified user experience for filtering contacts
- fixed bug where info banner on connect screen would show up as you went to tap screen
- fixed bug where you could enter more text in device name field than firmware would accept
- fixed bug where sending long channel messages would cut them shorter on recipient side

## v1.15.0 - 23/May/2025
- added sender name initials and emoji support to channel messages
- added colours, name initials and emoji support to contact list icons
- added button to telemetry screen to auto fetch telemetry every 10 seconds
- added ability to view app change log from about screen
- added ability to configure environment telemetry permissions
- added ability to tap battery percentage in app bar to show voltage
- added ability to turn off message notifications for specific channels
- added icons to advert dropdown menu
- fixed bug where user could set a blank name
- fixed bug where user could set a name with invalid characters
- fixed bug where increasing system font size would make app bar text too large
- fixed bug where telemetry permissions showed wrong contacts count for location permission
- fixed bug where contacts search would not clear when connecting to a different device
- improved bluetooth performance by upgrading internal library
- improved battery percentage calculation by using 3.0volts as 0% instead of 3.4volts
- renamed "Full Events" to "Debug Flags" in repeater and room server status screens
- ui improvements

## v1.14.0 - 19/May/2025
- added altitude to telemetry position ui if non zero
- added support for current, power and voltage telemetry types
- added support for temperature, humidity and barometric pressure telemetry types
- added new dropdown to view other telemetry channels
- added new debug logs screen in settings
- added new tab to repeater neighbours to view as a list
- added ability to show contact name labels on repeater neighbours map
- added device info to bottom of settings screen
- added "clear stats" command to repeater commands help
- increased message attempts from 3 to 5 when a direct path is set
- improved contacts sync to help prevent lost contacts

## v1.13.0 - 14/May/2025
- added new foreground service to android app to allow device to stay connected when app is minimised
- added new persistent notification to show what device is connected when app is minimised
- added support for updated repeater neighbours response with seconds ago, instead of timestamp
- improved ui for position and telemetry settings screens when they're not supported by connected device

## v1.12.0 - 11/May/2025
- added support for fetching telemetry from contacts (battery and position)
- added new position settings screen to configure gps mode
- added new telemetry settings screen to configure telemetry permissions
- added new contact permissions screen to configure specific contact permissions
- added ability to tap alert popups to dismiss them
- added button to go to contact details screen from local map contact popup
- added button to share contact from contact details screen
- added ability to purge all contacts from settings screen
- added dynamic battery icon to main app bar instead of battery text label
- added ability to change tile layers on all map screens
- added open topo map tile layer
- improved performance of syncing specific contacts when adverts are received or paths change
- fixed bug where ui was hidden under navigation bars on android 15+
- fixed bug where user could choose an invalid custom bluetooth pin
- fixed bug where ble connection was successful but connect screen didn't navigate to main screen
- fixed bug where chat message input was cleared when retrying a failed message
- fixed bug where unsupported map zoom levels caused invalid server requests to tile providers
- fixed bug where internal event listeners would fire in different order and cause lost contacts

## v1.11.0 - 02/May/2025
- added new settings screen to configure bluetooth pin
- added new screens to import and export companion configuration
- added new zero hop neighbours map to repeater remote management
- added hop count to long press menu for received channel messages
- added warning popup when pinging a contact if multiple contacts have same first byte
- added support for TCP/WiFi connections on Android/iOS
- added support for pressing enter to send messages on web (shift+enter to add new line)
- added haptic feedback when long pressing map and messages
- fixed bug where adding contacts manually would set path as direct instead of flood
- increased error message duration from 5 to 15 seconds

## v1.10.0 - 19/April/2025
- added ability to upload contacts to internet map
- added ability to see radio settings when tapping nodes on internet map
- added ability to update own radio settings to those used by a node on internet map
- added marker clustering support to local and internet maps to improve ux and performance
- added new contact settings screen
- added new setting to enable/disable auto adding contacts from adverts, requires firmware v1.5.0
- added new discover screen that shows recently received adverts, requires app to be connected when advert is received
- added ability to add contacts from discover screen
- added ability to activate premium features without an internet connection
- added distance away in contact details screen

## v1.9.1 - 06/April/2025
- fixed bug where timestamps for outgoing messages showed the wrong date

## v1.9.0 - 04/April/2025
- added new screen to view contact details
- added new screen to manage auto advert intervals for repeaters and room servers
- added ability to long press path trace map to set self position
- added ability to add and remove your companion node on the internet map
- added ability to zero hop ping repeaters and room servers from contacts list
- added ability to connect to usb companion on the web app
- timestamp under messages now shows sender timestamp instead of received timestamp
- fixed bug where keyboard would not dismiss when saving repeater and room server settings
- fixed bug where send button in os keyboard would not send cli command
- fixed ble connections to esp32 devices in web client

## v1.8.0 - 25/March/2025
- added confirmation dialog before exiting app when connected to a device
- added support for running app from web browsers
- long pressing contact messages now shows the timestamp the sender sent it at
- increased timeouts for cli commands and remote management advert buttons
- refactored internal database handling to only allow one database instance at a time
- fixed bluetooth bug where device would try to auto reconnect even if pairing failed

## v1.7.1 - 20/March/2025
- fixed bug where it wasn't possible to reboot esp32 devices when ble write without response was not supported
- fixed bug where repeaters layer was not selected by default in the trace path map screen
- fixed bug where negative snr values in trace path map, and list would show as 50dB+

## v1.7.0 - 19/March/2025
- added new tools section in main menu
- added new internet map screen to view publicly shared meshcore nodes (requires internet connection)
- added new path tracing feature, to view snr for each repeater hop (requires firmware v1.4.0+)
- added mutex lock to contact sync to prevent race condition causing contacts to get deleted
- added mutex lock to message sync to prevent race condition causing duplicate messages
- added room server status tab, currently for admins only, will need ui adjustments for guest access
- battery level is now fetched as soon as connected instead of waiting for contacts sync
- fixed bug where bluetooth would not auto reconnect if a previous reconnect failed
- fixed bug where removing an entry in custom path would remove all the commas
- fixed bug where keyboard would show when navigating back, if the field didn't have focus
- fixed margins on alert popups

## v1.6.0 - 14/March/2025
- added ability to share and scan contact qr codes
- added ability to view known repeaters for custom paths
- added button to remove specific repeater from custom path
- added new unique colours based on contact name for channel messages
- added button to show remembered password for repeater and room server login
- added unread message indicator to contact and channel tabs
- ui improvements for sharing contacts, and other menu cleanups
- fixed bug where qr codes where black in dark mode and could not be seen

## v1.5.0 - 13/March/2025
- NOTE: custom channels requires v1.3.0+ companion firmware
- added ability to create custom channels, which can be used for secure, mesh wide group chats
- added ability to add and share channels via qr code
- added ability to quickly add the public channel
- added option to order contacts list by most recent messages first
- added new menu to radio settings section, to allow picking from suggested radio settings
- added filter menu to search bar to select room servers when selecting path contacts
- added new satellite map tile layer from esri
- added attribution overlay for map tile providers
- decreased the size of the floating buttons on the map
- fixed some ui issues where small screens would not scroll down

## v1.4.1 - 07/March/2025
- added button to send message to a room server from the map screen
- implemented new companion protocol version to allow for showing received message snr
- when a message is received with snr information, it can be seen by long pressing the message
- duplicate messages are no longer shown when a senders retry is received for an existing message
- confirmation dialog buttons for deleting messages are now red and show on the right side
- fixed bug where contacts would disappear when a corrupted name was received

## v1.4.0 - 06/Mach/2025
- added last snr to repeater stats screen
- added new notification when a new contact is discovered
- added new notification settings screen to adjust preferences
- added auto retry of failed direct messages up to 3 attempts
- added message settings page to configure if last send attempt should send as flood
- added start ota command to repeater commands help list
- added info banner to manage identity screen to say custom firmware is needed
- contacts are now ordered most recently heard by default
- room server remote management screen now defaults to showing the settings tab
- improved error message when failing to connect to a half bonded ble device
- fixed bug where some unread message counts were showing incorrect values

## v1.3.0 - 04/March/2025
- added ability to send and receive room server messages
- added ability to view locally cached room server messages without a connection to room server
- added remote management support for room servers, command line and form based
- added notifications for new room server messages
- both sent and received timestamps for incoming room server messages are now shown in long press menu
- unread messages badge is now shown for room servers in contacts list
- devices with new v1.0.0+ firmware versions now show at bottom of settings page
- fixed some help entries in the repeater commands list

## v1.2.0 - 01/March/2025
- added new repeater settings page
- added ui to configure repeater name and radio settings
- added ui to set admin and guest password for repeaters
- added ui to set repeater position
- added button to sync repeater clock from current device
- added buttons to advert, sync clock and reboot repeaters via remote management
- added checkbox to remember admin password per repeater
- added info banner with default bluetooth pin on connect screen
- added new position selector screen to pick lat lon by tapping on map
- switched to using full keyboard layout for number inputs as some regions don't show dot for decimal inputs

## v1.1.0 - 28/February/2025
- added ability to log in to a repeater as an admin or a guest
- added ability for guests to view status of repeaters
- added new remote management feature for repeater admins
- added new quick commands help section to repeater management menu
- added button to remote manage repeaters from map popup menu
- added path to contact name subtitle when clicking a contact on the map
- fixed bug where adding a new contact would always add as chat, regardless of the contact type you selected
- other ui improvements

## v1.0.9 - 26/February/2025
- added red indicator icon to filter button when selected filters don't include all results
- contact search bar now shows how many contacts are in the list, based on selected filters
- fixed bug where app would not disconnect from BLE device when pairing fails, causing device to no longer be discoverable
- internal improvements to bluetooth connection handling
- increased connection timeout for fetching device info
- please upgrade to the latest firmware for the best experience
- ble devices are now manually bonded before trying to use them

## v1.0.8 - 25/February/2025
- added ability to filter contact types on map
- added ability to manually create contacts from a known public key
- added menu to advert button to show more options
- added button to export self advert to clipboard
- added icon for room servers
- contacts list now uses the local timestamp of when a contact was last heard
- fixed bug where private key field was not editable when managing identity key
- fixed bug where it wasn't possible to enter decimal point and minus sign in some settings fields
- fixed bug where transmit power was not saved when saving device settings

## v1.0.7 - 24/February/2025
- added ability to filter contacts list by type
- added device model and firmware build info to bottom of settings screen
- added link to meshcore website in about page
- clicking notifications will now open the relevant conversation
- fixed bug where unread messages badge would not update after connecting to different devices
- fixed bug where date was not showing for adverts that are not today

## v1.0.6 - 23/February/2025
- updated android package name to com.liamcottle.meshcore.android
- fixed button to copy public key from map menu
- added required plist values for ios app store

## v1.0.5 - 22/February/2025
- added new map feature
- added new about screen to main menu
- added ability to paste comma delimited lat/long into device settings page
- added ability to update device position by long pressing on the map
- all known contacts that have a valid position will be shown on the map

## v1.0.4 - 22/February/2025
- fixed issues where lat/long inputs in settings would not show decimal places
- moved disconnect button to top of main screen menu
- added ability to import and export private identity key when firmware has feature enabled
- improved internal contacts management
- direct messages screen now shows how many hops away the contact is
- added contact dropdown menu to messages screen
- added button to clear path input field
- added ability to order contacts alphabetically or by heard recently

## v1.0.3 - 20/February/2025
- added contact search
- added contact import from clipboard
- added buttons to export contact to clipboard
- add button to copy contact public key
- add button to share contact as zero hop advert
- added public key to settings page, with button to copy
- can now long press messages to view additional actions
- added button to retry failed messages
- added ability to set custom path for contacts
- can select repeaters when setting custom path
- fixed bug with header changing colour on scroll
- adjusted theme colours for floating action buttons

## v1.0.2 - 20/February/2025
- Added Dark Mode (automatic based on device theme)

## v1.0.1 - 20/February/2025
- added support for automatically reconnecting to device when it comes back into range.
- this is provided the app has not been killed. for now, if you force quit the app, you will need to manually connect on launch

## v1.0.0 - 19/February/2025
- initial release
