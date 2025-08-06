function compile_mex()
    % Compile MEX files for ultra-fast ArduPilot log processing
    
    fprintf('=== Compiling ArduPilot MEX Files ===\n\n');
    
    success_count = 0;
    
    fprintf('Compiling ArduPilot C parser...\n');
    try
        mex('-O', 'ardupilot_parse_log.c');
        
        % Test the complete parser with a simple, well-formed test case
        header = [163, 149];
        
        % Create a simple FMT message defining itself (self-referential)
        % Header + FMT_ID + Type + Length + Name + Format + Labels = 89 bytes total
        fmt_msg = [
            header, 128, ...           % Header [163, 149] + FMT message ID (128)
            128, 89, ...               % Type=128 (FMT), Length=89
            uint8('FMT'), 0, ...       % Name: "FMT" + null terminator (4 bytes total)
            uint8('BBnNZ'), zeros(1,11), ... % Format: "BBnNZ" + padding to 16 bytes
            uint8('TimeUS,Type,Length,Name,Format,Columns'), zeros(1,27) % Labels + padding to 64 bytes
        ];
        
        % Verify the message is exactly 89 bytes
        if length(fmt_msg) ~= 89
            fprintf('Warning: Test FMT message is %d bytes, expected 89\n', length(fmt_msg));
            % Pad or trim to exactly 89 bytes
            if length(fmt_msg) < 89
                fmt_msg = [fmt_msg, zeros(1, 89 - length(fmt_msg))];
            else
                fmt_msg = fmt_msg(1:89);
            end
        end
        
        test_data = uint8(fmt_msg);
        

        
        try
            logData = ardupilot_parse_log(test_data, header, []);
            if ~isempty(logData.fmt_messages)
                success_count = success_count + 1;
            end
        catch ME
            % Test failed but compilation succeeded
        end
        
    catch ME
        fprintf('Compilation failed: %s\n', ME.message);
    end
    

    
    if success_count >= 1
        fprintf('✓ C optimization ready\n');
    else
        fprintf('⚠ Compilation failed - using MATLAB fallback\n');
    end
end