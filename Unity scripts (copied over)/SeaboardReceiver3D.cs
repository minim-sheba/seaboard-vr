using UnityEngine;
using OscJack;

public class SeaboardReceiver : MonoBehaviour
{
    [Header("OSC Settings")]
    [SerializeField] private int port = 8000;
    [SerializeField] private string oscAddress = "/seaboard/note_active";

    [Header("Avatar Control")]
    [SerializeField] private PlayerMovement avatarMovementScript;
    [SerializeField] private float seaboardMoveSpeed = 8f; // Speed when moving via Seaboard

    private OscServer server;
    private bool currentNoteState = false;
    private bool isMovingForward = false;

    void Start()
    {
        // Find the PlayerMovement script if not assigned
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
        // Handle Seaboard movement on main thread
        if (isMovingForward && avatarMovementScript != null)
        {
            // Apply forward movement using the existing CharacterController
            Vector3 forwardMove = transform.forward * seaboardMoveSpeed * Time.deltaTime;
            avatarMovementScript.controller.Move(forwardMove);
            
            Debug.Log("Moving forward via Seaboard!");
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
        // Get the MIDI note value (0 = no notes, >0 = note playing)
        if (data.GetElementCount() > 0)
        {
            int noteActive = data.GetElementAsInt(0);
            currentNoteState = (noteActive > 0); // Any note value > 0 means a note is playing

            Debug.Log($"Received OSC: {address} = {noteActive}");

            // Set movement state based on any note being active
            isMovingForward = currentNoteState;
        }
    }

    // Optional: Display connection status in the inspector
    void OnGUI()
    {
        GUI.Label(new Rect(10, 10, 200, 20), $"OSC Port: {port}");
        GUI.Label(new Rect(10, 30, 200, 20), $"Note Active: {currentNoteState}");
        GUI.Label(new Rect(10, 50, 200, 20), $"Moving Forward: {isMovingForward}");
    }
}