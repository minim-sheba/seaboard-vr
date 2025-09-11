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
    [SerializeField] private float glideSensitivity = 10f; // Sensitivity for glide value

    private OscServer server;
    private bool currentNoteState = false;
    private bool isMovingForward = false;
    private float currentGlideValue = 0f;

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
            // Forward movement
            Vector3 forwardMove = avatarMovementScript.transform.forward * seaboardMoveSpeed * Time.deltaTime;
            avatarMovementScript.controller.Move(forwardMove);

            // Glide rotation
            float glideRotation = currentGlideValue * glideSensitivity * Time.deltaTime;
            avatarMovementScript.transform.Rotate(0, glideRotation, 0);
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
        if (data.GetElementCount() > 1) // Check we have at least 2 values
        {
            int noteActive = data.GetElementAsInt(0);    // First value
            float glideValue = data.GetElementAsFloat(1); // Second value

            currentNoteState = (noteActive > 0);
            currentGlideValue = glideValue; // Store for use in Update()
            
            Debug.Log($"Received OSC: {address} = {noteActive}");
            Debug.Log($"Received Glide Value: {glideValue}");

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