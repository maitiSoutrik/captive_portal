$(document).ready(function() {
    const messageArea = $('#message-area');

    function showMessage(message, type = 'info') {
        messageArea.text(message).removeClass('success error info').addClass(type).show();
        // Automatically hide after 5 seconds
        setTimeout(function() {
            messageArea.fadeOut();
        }, 5000);
    }

    // Function to fetch and display card count
    function getCardCount() {
        $('#card-count').text('Loading...');
        $.ajax({
            url: '/cards/Count', // Updated URI
            type: 'GET',
            success: function(response) {
                $('#card-count').text(response.count);
            },
            error: function() {
                $('#card-count').text('Error');
                showMessage('Error fetching card count.', 'error');
            }
        });
    }

    // Function to fetch and display card list
    function listCards() {
        $('#cards-table-body').html('<tr><td colspan="4">Loading cards...</td></tr>');
        $.ajax({
            url: '/cards/Get', // Updated URI
            type: 'GET',
            success: function(response) {
                console.log("Response from /cards/Get:", response); // Debugging line
                var tableBody = $('#cards-table-body');
                tableBody.empty();
                // Assuming response is the JSON string directly, parse it.
                // If response is already an object (jQuery might auto-parse), this might not be needed or could error.
                var data;
                if (typeof response === 'string') {
                    try {
                        data = JSON.parse(response);
                    } catch (e) {
                        showMessage('Error parsing card list data.', 'error');
                        console.error("JSON Parse Error:", e, "Response:", response);
                        tableBody.html('<tr><td colspan="4">Error parsing card data.</td></tr>');
                        return;
                    }
                } else {
                    data = response; // Assume it's already an object
                }

                if (data.cards && data.cards.length > 0) {
                    data.cards.forEach(function(card) {
                        // Use 'id', 'nm', 'ts' as per C code's JSON generation
                        var row = '<tr>' +
                            '<td>' + card.id + '</td>' + 
                            '<td>' + (card.nm || '') + '</td>' +
                            '<td>' + (card.ts ? new Date(card.ts * 1000).toLocaleString() : 'N/A') + '</td>' +
                            '<td><button class="remove-card-btn danger-btn" data-card-id="' + card.id + '">Remove</button></td>' +
                            '</tr>';
                        tableBody.append(row);
                    });
                } else {
                    tableBody.html('<tr><td colspan="4">No cards found.</td></tr>');
                }
            },
            error: function() {
                $('#cards-table-body').html('<tr><td colspan="4">Error loading cards.</td></tr>');
                showMessage('Error fetching card list.', 'error');
            }
        });
    }

    // Add card form submission
    $('#add-card-form').on('submit', function(event) {
        event.preventDefault();
        var cardIdStr = $('#card-id').val(); // Keep as string for validation
        var cardName = $('#card-name').val();
        
        // Validate decimal input
        if (!/^[0-9]{1,10}$/.test(cardIdStr) || parseInt(cardIdStr, 10) > 4294967295) { // Max uint32_t
            showMessage('Invalid Card ID. Please use a number between 0 and 4294967295.', 'error');
            return;
        }
        var cardId = parseInt(cardIdStr, 10); // Parse as decimal

        if (cardName.length === 0 || cardName.length > 31) {
            showMessage('Cardholder Name must be between 1 and 31 characters.', 'error');
            return;
        }

        $.ajax({
            url: '/cards/Add', // Updated URI
            type: 'POST',
            contentType: 'application/json',
            // The C code expects 'id' and 'nm' for add card
            data: JSON.stringify({ id: cardId, nm: cardName }), 
            success: function() {
                showMessage('Card added successfully!', 'success');
                $('#add-card-form')[0].reset();
                getCardCount(); // Refresh count
                listCards();    // Refresh list
            },
            error: function(xhr) {
                var errorMsg = 'Error adding card.';
                if (xhr.responseJSON && xhr.responseJSON.error) {
                    errorMsg += ' ' + xhr.responseJSON.error;
                } else if (xhr.statusText) {
                    errorMsg += ' ' + xhr.statusText;
                }
                showMessage(errorMsg, 'error');
            }
        });
    });

    // Remove card button click
    $(document).on('click', '.remove-card-btn', function() {
        var cardId = $(this).data('card-id'); // This will be a number
        // Using a custom confirmation dialog or inline confirmation would be better than confirm()
        // For now, keeping confirm() but ideally this would also use the messageArea or a modal.
        if (confirm('Are you sure you want to remove card ID: ' + cardId + '?')) { // Show decimal in confirm
            $.ajax({
                url: '/cards/Delete?id=' + cardId, 
                type: 'DELETE',
                success: function() {
                    showMessage('Card removed successfully!', 'success');
                        getCardCount(); // Refresh count
                        listCards();    // Refresh list
                    },
                    error: function(xhr) {
                        var errorMsg = 'Error removing card.';
                         if (xhr.responseJSON && xhr.responseJSON.error) {
                            errorMsg += ' ' + xhr.responseJSON.error;
                        } else if (xhr.statusText) {
                            errorMsg += ' ' + xhr.statusText;
                        }
                        showMessage(errorMsg, 'error');
                    }
                });
            }
    });
    
    // Reset RFID database button click
    $('#reset-rfid-btn').on('click', function() {
        if (confirm('WARNING: This will delete all current RFID cards and load defaults. Are you sure?')) {
            $.ajax({
                url: '/cards/Reset', // Updated URI
                type: 'POST',
                success: function() {
                    showMessage('RFID database reset to defaults successfully!', 'success');
                        getCardCount(); // Refresh count
                        listCards();    // Refresh list
                    },
                    error: function(xhr) {
                        var errorMsg = 'Error resetting RFID database.';
                         if (xhr.responseJSON && xhr.responseJSON.error) {
                            errorMsg += ' ' + xhr.responseJSON.error;
                        } else if (xhr.statusText) {
                            errorMsg += ' ' + xhr.statusText;
                        }
                        showMessage(errorMsg, 'error');
                    }
                });
            }
    });

    // Button click handlers
    $('#refresh-count-btn').on('click', getCardCount);
    $('#list-cards-btn').on('click', listCards);

    // Initial data load
    getCardCount();
    listCards();

    // Periodic refresh
    setInterval(getCardCount, 10000); // Refresh card count every 10 seconds
    setInterval(listCards, 30000);   // Refresh card list every 30 seconds

    // Remove card by ID form submission
    $('#remove-card-by-id-form').on('submit', function(event) {
        event.preventDefault();
        var cardIdStr = $('#remove-card-id').val();
        if (!/^[0-9]{1,10}$/.test(cardIdStr) || parseInt(cardIdStr, 10) > 4294967295) {
            showMessage('Invalid Card ID. Please use a number between 0 and 4294967295.', 'error');
            return;
        }
        var cardId = parseInt(cardIdStr, 10);

        if (confirm('Are you sure you want to remove card ID: ' + cardId + '?')) {
            $.ajax({
                url: '/cards/Delete?id=' + cardId,
                type: 'DELETE',
                success: function() {
                    showMessage('Card ' + cardId + ' removed successfully!', 'success');
                    $('#remove-card-by-id-form')[0].reset();
                    getCardCount(); // Refresh count
                    listCards();    // Refresh list
                },
                error: function(xhr) {
                    var errorMsg = 'Error removing card ' + cardId + '.';
                    if (xhr.responseJSON && xhr.responseJSON.error) {
                        errorMsg += ' ' + xhr.responseJSON.error;
                    } else if (xhr.statusText) {
                        errorMsg += ' ' + xhr.statusText;
                    }
                    showMessage(errorMsg, 'error');
                }
            });
        }
    });

    // Check card existence form submission
    $('#check-card-form').on('submit', function(event) {
        event.preventDefault();
        var cardIdStr = $('#check-card-id').val();
        var statusSpan = $('#check-card-status');

        statusSpan.text('Checking...');

        if (!/^[0-9]{1,10}$/.test(cardIdStr) || parseInt(cardIdStr, 10) > 4294967295) {
            showMessage('Invalid Card ID for checking. Please use a number between 0 and 4294967295.', 'error');
            statusSpan.text('Invalid ID format.');
            return;
        }
        var cardId = parseInt(cardIdStr, 10);

        $.ajax({
            url: '/cards/Check', // Assumes C server endpoint is /cards/Check
            type: 'POST',
            contentType: 'application/json',
            data: JSON.stringify({ card_id: cardIdStr }), // Send as string, C-side parses with strtoul
            success: function(response) {
                if (response.exists) {
                    statusSpan.text('Card ID ' + cardId + ' exists and is active.');
                    showMessage('Card ID ' + cardId + ' exists.', 'success');
                } else {
                    statusSpan.text('Card ID ' + cardId + ' does not exist or is not active.');
                    showMessage('Card ID ' + cardId + ' does not exist.', 'info');
                }
            },
            error: function(xhr) {
                var errorMsg = 'Error checking card ' + cardId + '.';
                if (xhr.responseJSON && xhr.responseJSON.error) {
                    errorMsg += ' ' + xhr.responseJSON.error;
                } else if (xhr.statusText) {
                    errorMsg += ' ' + xhr.statusText;
                }
                showMessage(errorMsg, 'error');
                statusSpan.text('Error during check.');
            }
        });
    });
});
