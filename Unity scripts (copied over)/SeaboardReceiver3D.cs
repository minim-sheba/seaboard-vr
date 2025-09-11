using UnityEngine;
using OscJack;

public class SeaboardReceiver : MonoBehaviour
{
    [Header("OSC Settings")]
    [SerializeField] private int port = 8000;
    [SerializeField] private string oscAddress = "/seaboard/note_active";

    [Header("Avatar Control")]
    [SerializeField] private PlayerMovement avatarMovementScript;

    private OscServer server;
    private bool lastNoteState = false;
    private bool currentNoteState = false;
    private bool pendingFlap = false;

    void Start()
    {
        // Find the bird script if not assigned
        if (avatarMovementScript == null)
        {
            GameObject avatar = GameObject.Find("Player");
            if (avatar != null)
            {
                avatarMovementScript = avatar.GetComponent<PlayerMovement>();
            }
            else
            {
                Debug.LogError("Could not find Player GameObject!");
            }
        }

        // Set up OSC server
        server = new OscServer(port);
        server.MessageDispatcher.AddCallback(oscAddress, OnNoteActiveReceived);

        Debug.Log($"OSC Server started on port {port}, listening for {oscAddress}");
    }

    void Update()
    {
        // Handle pending flap on main thread
        if (pendingFlap)
        {
            TriggerBirdFlap();
            pendingFlap = false;
        }
    }

    void OnDestroy()
    {
        if (server != null)
        {
            server.Dispose();
        }
    }

    private void OnNoteActiveReceived(string address, OscDataHandle data)
    {
        // Get the value (should be 0 or 1)
        if (data.GetElementCount() > 0)
        {
            int noteActive = data.GetElementAsInt(0);
            currentNoteState = (noteActive > 1);

            Debug.Log($"Received OSC: {address} = {noteActive}");

            // Detect note press (transition from false to true)
            if (currentNoteState && !lastNoteState)
            {
                // Queue flap to execute on main thread
                pendingFlap = true;
            }

            lastNoteState = currentNoteState;
        }
    }

    private void TriggerBirdFlap()
    {
        if (avatarMovementScript != null)
        {
            Debug.Log("Triggering bird flap from Seaboard!");
            //avatarMovementScript.myRigidbody.linearVelocity = Vector2.up * avatarMovementScript.flapStrength;
        }
    }

    // Optional: Display connection status in the inspector
    void OnGUI()
    {
        GUI.Label(new Rect(10, 10, 200, 20), $"OSC Port: {port}");
        GUI.Label(new Rect(10, 30, 200, 20), $"Note Active: {currentNoteState}");
    }
}