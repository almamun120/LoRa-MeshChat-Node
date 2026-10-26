// Interop check between the app's Kotlin core and the C++ relay core.
//   Interop gen    <out>   : writes packets made by the REAL app code (identity, hello, announce, SOS, private message)
//   Interop verify <in>    : checks packets made by the relay (decode, signatures, decryption) with the REAL app code
import com.meshchat.core.*
import java.io.File
import java.math.BigInteger
import java.security.interfaces.ECPrivateKey

const val TEXT = "salam, ei message ta bangla te: নমস্কার ও আসসালামু আলাইকুম"

fun scalar(id: Identity): ByteArray {
    val s: BigInteger = (id.privateKey as ECPrivateKey).s
    val raw = s.toByteArray().dropWhile { it.toInt() == 0 }.toByteArray()
    return ByteArray(32 - raw.size) + raw
}

fun hex(b: ByteArray) = Hex.encode(b)
fun pkt(type: PacketType, src: String, dst: String, payload: ByteArray, ttl: Int, id: Identity?, msgId: ByteArray = ByteArray(8).also { java.security.SecureRandom().nextBytes(it) }): MeshPacket {
    val p = MeshPacket(type, 0, ttl, 0, msgId, src, dst, 1760000000123L, payload, null)
    return if (id != null) p.withSignature(CryptoService.sign(id, p.signedBytes())) else p
}

fun gen(out: String) {
    val a = Identity.generate()      // a phone
    val d = Identity.generate()      // the relay's identity (the C++ side loads its private key from this file)
    val lines = ArrayList<String>()
    lines += "A_ID=${a.nodeId}"
    lines += "D_ID=${d.nodeId}"
    lines += "A_SPKI=${hex(a.publicBytes)}"
    lines += "D_SPKI=${hex(d.publicBytes)}"
    lines += "D_PRIV=${hex(scalar(d))}"
    lines += "A_PRIV=${hex(scalar(a))}"
    lines += "TEXT=${hex(TEXT.toByteArray(Charsets.UTF_8))}"
    val ident = pkt(PacketType.IDENTITY, a.nodeId, NodeIds.BROADCAST,
        Payloads.encodeIdentity(Payloads.Identity(Cap.ALL, "Rahim", a.publicBytes, "rahim@example.com")), 1, a)
    lines += "A_IDENTITY=${hex(ident.encode())}"
    val hello = pkt(PacketType.HELLO, a.nodeId, d.nodeId,
        Payloads.encodeHello(Payloads.Hello("Rahim", Cap.ALL, listOf(RouteAd("0A0B0C0D", 2)))), 1, null)
    lines += "A_HELLO=${hex(hello.encode())}"
    val ann = pkt(PacketType.ANNOUNCE, a.nodeId, NodeIds.BROADCAST, Payloads.encodeAnnounce(Payloads.Announce("Rahim", "public post: " + TEXT)), 7, a)
    lines += "A_ANNOUNCE=${hex(ann.encode())}"
    val sos = pkt(PacketType.SOS, a.nodeId, NodeIds.BROADCAST, SosCodec.encode(SosPayload(true, 64, SosLocation(23.810331, 90.412521, 12), "Rahim")), 7, a)
    lines += "A_SOS=${hex(sos.encode())}"
    val id = ByteArray(8).also { java.security.SecureRandom().nextBytes(it) }
    val plain = ContentCodec.encode(Content.ofText(TEXT))!!
    val blob = CryptoService.encrypt(a, d.publicBytes, d.nodeId, CryptoService.aad(a.nodeId, d.nodeId, id), plain)
    lines += "A_TO_D=${hex(MeshPacket(PacketType.PRIVATE, 0, 8, 0, id, a.nodeId, d.nodeId, 1760000000123L, blob).encode())}"
    // a reply with an envelope (reply reference + disappearing timer) and a group message, as the real app sends them
    val id2 = ByteArray(8).also { java.security.SecureRandom().nextBytes(it) }
    val rich = ContentCodec.encode(Content.ofText("with envelope").copyEnvelope(ttlSec = 3600, replyToId = "0102030405060708", replyQuote = "quoted"))!!
    val blob2 = CryptoService.encrypt(a, d.publicBytes, d.nodeId, CryptoService.aad(a.nodeId, d.nodeId, id2), rich)
    lines += "A_TO_D_ENV=${hex(MeshPacket(PacketType.PRIVATE, 0, 8, 0, id2, a.nodeId, d.nodeId, 1760000000123L, blob2).encode())}"
    val sample = MeshPacket(PacketType.PRIVATE, 0, 8, 0, id2, a.nodeId, d.nodeId, 1760000000123L, blob2).encode()
    lines += "FRAMES=" + Fragmenter().split(sample, 100).joinToString(",") { hex(it) }
    lines += "FRAMES_SRC=${hex(sample)}"
    File(out).writeText(lines.joinToString("\n") + "\n")
    println("kotlin: wrote $out (A=${a.nodeId} D=${d.nodeId})")
}

fun verify(inp: String, ktOut: String) {
    val kv = File(inp).readLines().filter { it.contains('=') }.associate { it.substringBefore('=') to it.substringAfter('=') }
    val k = File(ktOut).readLines().filter { it.contains('=') }.associate { it.substringBefore('=') to it.substringAfter('=') }
    var fails = 0
    fun check(name: String, ok: Boolean) { println((if (ok) "  ok   " else "  FAIL ") + name); if (!ok) fails++ }

    val dSpki = Hex.decode(k["D_SPKI"]!!)
    val dId = k["D_ID"]!!
    val aSpki = Hex.decode(k["A_SPKI"]!!)
    // we need A's private key to decrypt D->A, so the generator wrote nothing for it: re-derive via the file the C++ side echoes
    val ident = MeshPacket.decode(Hex.decode(kv["D_IDENTITY"]!!))
    check("relay IDENTITY decodes", ident != null && ident.type == PacketType.IDENTITY)
    if (ident != null) {
        val ip = Payloads.decodeIdentity(ident.payload)
        check("relay IDENTITY payload decodes, name=${ip?.name}", ip != null && ip.name == "TestRelay")
        check("relay Node ID = SHA-256(public key)[0..4]", ip != null && NodeIds.fromPublicKey(ip.publicKey) == ident.src && ident.src == dId)
        check("relay IDENTITY signature verifies (SHA256withECDSA/DER)", ip != null && ident.signature != null && CryptoService.verify(ip.publicKey, ident.signedBytes(), ident.signature!!))
        check("relay IDENTITY public key == X.509 key the file announced", ip != null && ip.publicKey.contentEquals(dSpki))
    }
    val ann = MeshPacket.decode(Hex.decode(kv["D_ANNOUNCE"]!!))
    check("relay ANNOUNCE decodes + verifies", ann != null && ann.signature != null && CryptoService.verify(dSpki, ann.signedBytes(), ann.signature!!) &&
        Payloads.decodeAnnounce(ann.payload)?.let { it.authorName == "Sumi (web)" && it.content == "board: " + TEXT } == true)
    val hello = MeshPacket.decode(Hex.decode(kv["D_HELLO"]!!))
    val h = hello?.let { Payloads.decodeHello(it.payload) }
    check("relay HELLO decodes (name=${h?.name}, ${h?.routes?.size} routes)", hello != null && h != null && h.name == "TestRelay" && hello.src == dId)
    check("relay HELLO advertises route to 0A0B0C0D", h != null && h.routes.any { it.nodeId == "0A0B0C0D" && it.hops == 3 })
    val ack = MeshPacket.decode(Hex.decode(kv["D_ACK"]!!))
    check("relay ACK decodes and carries the acked message id", ack != null && ack.type == PacketType.ACK && ack.dst == k["A_ID"] && hex(ack.payload) == kv["ACKED_ID"])
    // D -> A private: A's private key is not in the file, so the generator also stores it for the test only
    val aPriv = Hex.decode(k["A_PRIV"]!!)
    val aPkcs8 = pkcs8(aPriv)
    val a = Identity.restore(aPkcs8, aSpki)
    check("test identity A restored, id matches", a.nodeId == k["A_ID"])
    val pm = MeshPacket.decode(Hex.decode(kv["D_TO_A"]!!))
    check("relay PRIVATE decodes", pm != null && pm.type == PacketType.PRIVATE && pm.dst == a.nodeId && pm.src == dId)
    if (pm != null) {
        val plain = CryptoService.decrypt(a, dSpki, dId, CryptoService.aad(pm.src, pm.dst, pm.msgId), pm.payload)
        val c = plain?.let { ContentCodec.decode(it) }
        check("relay PRIVATE decrypts with the app's code and reads: ${c?.text}", c != null && c.kind == ContentKind.TEXT && c.text == "[Web:Sumi] " + TEXT)
    }
    println(if (fails == 0) "KOTLIN VERIFY: ALL OK" else "KOTLIN VERIFY: $fails FAILED")
    if (fails > 0) System.exit(1)
}

fun pkcs8(scalar: ByteArray): ByteArray {
    // PKCS#8 wrapper for a P-256 private scalar without the optional public key (Java accepts it)
    val ecPriv = byteArrayOf(0x30, 0x25, 0x02, 0x01, 0x01, 0x04, 0x20) + scalar
    val algo = Hex.decode("301306072A8648CE3D020106082A8648CE3D030107")
    val inner = byteArrayOf(0x02, 0x01, 0x00) + algo + byteArrayOf(0x04, ecPriv.size.toByte()) + ecPriv
    return byteArrayOf(0x30, inner.size.toByte()) + inner
}

fun main(args: Array<String>) {
    when (args[0]) {
        "gen" -> gen(args[1])
        "verify" -> verify(args[1], args[2])
        else -> error("usage")
    }
}
