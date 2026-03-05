import android.content.Context
import android.media.AudioManager
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import java.io.OutputStream
import java.net.Socket
import org.json.JSONObject

fun sendDeviceInfo(context: Context) {
    val serverIP = "SERVER_PUBLIC_IP"
    val port = 5000

    val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    val volume = audioManager.getStreamVolume(AudioManager.STREAM_MUSIC).toFloat() /
                 audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)

    val connectivityManager = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    val network = connectivityManager.activeNetwork
    val capabilities = connectivityManager.getNetworkCapabilities(network)
    val bandwidth = when {
        capabilities == null -> 0.0f
        capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> 1.0f
        capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) -> 0.5f
        else -> 0.3f
    }

    val json = JSONObject()
    json.put("os", "Android ${android.os.Build.VERSION.RELEASE}")
    json.put("device_type", "mobile")
    json.put("volume", volume)
    json.put("bandwidth", bandwidth)

    Thread {
        try {
            Socket(serverIP, port).use { socket ->
                val out: OutputStream = socket.getOutputStream()
                out.write(json.toString().toByteArray())
                out.flush()
            }
            println("Info Android enviada!")
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }.start()
}
