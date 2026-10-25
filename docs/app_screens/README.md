# MeshChat অ্যাপে LoRa যেভাবে দেখায় (স্ক্রিন-নকশা)

এই ছবিগুলো **আসল ফোনের স্ক্রিনশট নয়**। অ্যাপের Compose কোড (`TabScreens.kt`, `ConversationScreens.kt`, `MainActivity.kt`)
পড়ে একই লেখা, লেআউট ও নিয়ম অনুযায়ী আঁকা, নমুনা নাম/মান দিয়ে। রং ফোনের ডাইনামিক থিমে বদলাতে পারে।

- `00_overview.png` — চারটি স্ক্রিন একসাথে
- `01_home.png` — Home (উপরে "Mesh: N nodes")
- `02_nearby_active.png` — Nearby → Active: LoRa-র ডিভাইসে "via LoRa relay", আবহাওয়া-রিলে 🌡 তাপমাত্রা ও চাপ
- `03_nearby_inactive.png` — Nearby → Inactive
- `04_chat_lora.png` — LoRa-ডিভাইসের চ্যাট: কল/ফটো/ভয়েস বন্ধ, শুধু টেক্সট ও লোকেশন

দূরত্ব LoRa-তে প্রতি হপে ≈ 20 m ধরে হিসাব হয়, তাই ওপারের ফোন কিলোমিটার দূরে থাকলেও "≈ 60 m" দেখাতে পারে।
