# AWS IoT Core Setup Guide

This guide provides a step-by-step process for setting up AWS IoT Core to work with the `captive_portal` project.

## 1. Create a "Thing" in AWS IoT Core

A "Thing" is a virtual representation of your device in the AWS IoT Core registry.

1.  **Navigate to AWS IoT Core**:
    *   Log in to your AWS Management Console.
    *   Go to the "IoT Core" service.

2.  **Create a Thing**:
    *   In the left-hand navigation pane, go to **Manage** > **Things**.
    *   Click **Create things**.
    *   Select **Create single thing**.
    *   Give your Thing a name (e.g., `captive_portal_device`).
    *   Leave the other settings as default and click **Next**.

## 2. Generate Security Certificates

Your device needs security certificates to authenticate with AWS IoT Core.

1.  **Configure Device Certificate**:
    *   On the "Configure device certificate" page, select **Auto-generate a new certificate (recommended)**.
    *   Click **Next**.

2.  **Create and Attach Policies**:
    *   You'll need to create a policy that grants your device the necessary permissions. Click **Create policy**.
    *   Give the policy a name (e.g., `captive_portal_policy`).
    *   In the "Policy document", add the following JSON. This is a basic policy; you should restrict it further for production environments.

    ```json
    {
      "Version": "2012-10-17",
      "Statement": [
        {
          "Effect": "Allow",
          "Action": [
            "iot:Connect",
            "iot:Publish",
            "iot:Subscribe",
            "iot:Receive"
          ],
          "Resource": "*"
        }
      ]
    }
    ```

    *   Click **Create**.
    *   Back on the "Create thing" page, select the policy you just created and click **Create thing**.

3.  **Download Certificates**:
    *   You will now see a page with your new certificates. **Download all of them**, especially:
        *   **Device certificate** (`...-certificate.pem.crt`)
        *   **Private key file** (`...-private.pem.key`)
        *   **Root CA certificate** (e.g., `AmazonRootCA1.pem`)
    *   **This is your only chance to download the private key.** Keep these files secure.

## 3. Configure IAM Policies (If Needed)

If you are using other AWS services, you might need to configure IAM policies for your user or role. For basic IoT Core functionality, the Thing policy is often sufficient.

## 4. Next Steps

With the "Thing" created and certificates downloaded, you can now configure your ESP32 device to connect to AWS IoT Core using these credentials. You will need to embed the certificates into your firmware or store them in a secure way on the device.
