# Arduino IDE-তে কম্পাইল (MeshChat LoRa Relay 1.5)

1. **Arduino IDE 2.x** নিন। Boards Manager থেকে **"esp32 by Espressif Systems" সংস্করণ 2.0.17** ইনস্টল করুন (3.x নয়)।
2. Library Manager থেকে ইনস্টল (সংস্করণ মিলিয়ে):
   - NimBLE-Arduino **1.4.3** (h2zero)
   - RadioLib **6.6.0** (Jan Gromeš)
   - ArduinoJson **7.2.1** (Benoit Blanchon)
   - LiquidCrystal_I2C **1.1.4** (marcoschwartz; না মিললে GitHub থেকে ZIP: github.com/marcoschwartz/LiquidCrystal_I2C)
3. **C++17 চালু করুন:** esp32 2.0.17 ফোল্ডারে (Windows: `%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\2.0.17\`)
   `platform.local.txt` নামে ফাইল বানিয়ে এক লাইন লিখুন:
   `compiler.cpp.extra_flags=-std=gnu++17`
   তারপর IDE বন্ধ করে আবার খুলুন।
4. `MeshChat_Relay` ফোল্ডারটি Arduino/sketchbook-এ রেখে `MeshChat_Relay.ino` খুলুন।
5. Tools: **Board = ESP32 Dev Module**, **Partition Scheme = Huge APP (3MB No OTA/1MB SPIFFS)**, Upload Speed 921600, Port = আপনার পোর্ট।
6. LCD ব্যাকপ্যাক ছাড়া (১৬ পিন) হলে `config.h`-এর শুরুতে `#define LCD_I2C 1` কে `0` করুন।
7. Verify → Upload। Serial Monitor 115200-এ `MeshChat relay 1.5` দেখা গেলে ঠিক।

**একটি পার্থক্য:** PlatformIO-তে NimBLE-র সর্বোচ্চ সংযোগ ৫ (৪টি ফোন + ১টি ব্লুটুথ কীবোর্ড) করা আছে (`CONFIG_BT_NIMBLE_MAX_CONNECTIONS=5`)। Arduino IDE-তে ফ্ল্যাগ দেওয়া যায় না, তাই লাইব্রেরির ডিফল্ট (৩) চলবে: ২টি ফোন + কীবোর্ড। ৪ ফোন + কীবোর্ড চাইলে `Arduino/libraries/NimBLE-Arduino/src/nimconfig.h`-এ `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` ৫ করুন।
